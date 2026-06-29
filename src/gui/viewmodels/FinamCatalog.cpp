#include "gui/viewmodels/FinamCatalog.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace hftrec::gui::detail {
namespace {

QString finamCatalogSourcePath() {
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../src/gui/data/finam_assets.json"));
}

QByteArray readFinamCatalogBytes() {
#if defined(HFTRREC_SOURCE_DIR)
    QFile sourceFile(QStringLiteral(HFTRREC_SOURCE_DIR) + QStringLiteral("/src/gui/data/finam_assets.json"));
    if (sourceFile.open(QIODevice::ReadOnly)) return sourceFile.readAll();
#endif

    QFile appRelativeFile(finamCatalogSourcePath());
    if (appRelativeFile.open(QIODevice::ReadOnly)) return appRelativeFile.readAll();

    QFile resourceFile(QStringLiteral(":/HftRecorder/data/finam_assets.json"));
    if (resourceFile.open(QIODevice::ReadOnly)) return resourceFile.readAll();
    return {};
}

QString finamIndexKey(QString value) {
    return value.trimmed().toUpper();
}

void insertFirstFinamRow(QHash<QString, const FinamSymbolRow*>& index,
                         const QString& rawKey,
                         const FinamSymbolRow& row) {
    const QString key = finamIndexKey(rawKey);
    if (key.isEmpty() || index.contains(key)) return;
    index.insert(key, &row);
}

FinamCatalog loadFinamCatalog() {
    const QByteArray bytes = readFinamCatalogBytes();
    if (bytes.isEmpty()) return {};
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QJsonArray assets = doc.object().value(QStringLiteral("assets")).toArray();
    FinamCatalog catalog;
    catalog.rows.reserve(static_cast<std::size_t>(assets.size()));
    for (const auto& raw : assets) {
        const QJsonObject item = raw.toObject();
        FinamSymbolRow row{};
        row.symbol = item.value(QStringLiteral("symbol")).toString().trimmed();
        row.ticker = item.value(QStringLiteral("ticker")).toString().trimmed();
        row.name = item.value(QStringLiteral("name")).toString().trimmed();
        row.mic = item.value(QStringLiteral("mic")).toString().trimmed();
        row.market = item.value(QStringLiteral("market")).toString().trimmed().toLower();
        row.type = item.value(QStringLiteral("type")).toString().trimmed();
        row.underlying = item.value(QStringLiteral("underlying")).toString().trimmed().toUpper();
        row.expiration = item.value(QStringLiteral("expiration")).toString().trimmed();
        row.isArchived = item.value(QStringLiteral("is_archived")).toBool(false);
        const auto contractValue = item.value(QStringLiteral("contract_size"));
        if (contractValue.isDouble()) row.contractSize = QString::number(contractValue.toDouble());
        else row.contractSize = contractValue.toString().trimmed();
        if (row.symbol.isEmpty() || row.market.isEmpty()) continue;
        catalog.rows.push_back(std::move(row));
    }

    catalog.spotRows.reserve(catalog.rows.size());
    catalog.futuresRows.reserve(catalog.rows.size());
    for (const FinamSymbolRow& row : catalog.rows) {
        insertFirstFinamRow(catalog.bySymbol, row.symbol, row);
        insertFirstFinamRow(catalog.byTicker, row.ticker, row);
        if (row.market == QStringLiteral("spot")) {
            catalog.spotRows.push_back(&row);
        } else if (row.market == QStringLiteral("futures")) {
            catalog.futuresRows.push_back(&row);
            if (!row.underlying.isEmpty()) {
                catalog.futuresByUnderlying[row.underlying].push_back(&row);
            }
        }
    }
    return catalog;
}

bool matchesQueryBoundary(const QString& text, const QString& query) {
    if (query.isEmpty()) return true;
    qsizetype from = 0;
    while (from < text.size()) {
        const qsizetype idx = text.indexOf(query, from, Qt::CaseInsensitive);
        if (idx < 0) return false;
        const QChar prev = idx > 0 ? text.at(idx - 1) : QChar{};
        const qsizetype afterIdx = idx + query.size();
        const QChar next = afterIdx < text.size() ? text.at(afterIdx) : QChar{};
        if (idx == 0 || !prev.isLetterOrNumber() || prev.isDigit() || prev == QLatin1Char('@') ||
            !next.isLetterOrNumber() || next.isDigit()) {
            return true;
        }
        from = idx + 1;
    }
    return false;
}

}  // namespace

const FinamCatalog& finamCatalog() {
    static const FinamCatalog catalog = loadFinamCatalog();
    return catalog;
}

const std::vector<FinamSymbolRow>& finamSymbolRows() {
    return finamCatalog().rows;
}

const std::vector<const FinamSymbolRow*>& finamRowsForMarket(const QString& market) {
    static const std::vector<const FinamSymbolRow*> empty;
    const FinamCatalog& catalog = finamCatalog();
    const QString normalized = market.trimmed().toLower();
    if (normalized == QStringLiteral("spot")) return catalog.spotRows;
    if (normalized == QStringLiteral("futures")) return catalog.futuresRows;
    return empty;
}

bool finamRowMatchesMarket(const FinamSymbolRow& row, const QString& market) {
    return row.market == market.trimmed().toLower();
}

bool finamTextMatchesQuery(const FinamSymbolRow& row, const QString& query) {
    const auto normalized = query.trimmed();
    if (normalized.isEmpty()) return true;
    return matchesQueryBoundary(row.symbol, normalized) ||
           matchesQueryBoundary(row.ticker, normalized) ||
           matchesQueryBoundary(row.name, normalized) ||
           matchesQueryBoundary(row.underlying, normalized);
}

QString finamAnchorUnderlying(const QString& anchorSymbolText) {
    const auto symbols = normalizedSymbols(anchorSymbolText);
    if (symbols.empty()) return {};
    const QString anchor = QString::fromStdString(symbols.front()).trimmed().toUpper();
    const FinamCatalog& catalog = finamCatalog();
    if (auto it = catalog.bySymbol.constFind(anchor); it != catalog.bySymbol.constEnd()) {
        const FinamSymbolRow* row = it.value();
        if (row != nullptr) return row->underlying.isEmpty() ? row->ticker.toUpper() : row->underlying;
    }
    if (auto it = catalog.byTicker.constFind(anchor); it != catalog.byTicker.constEnd()) {
        const FinamSymbolRow* row = it.value();
        if (row != nullptr) return row->underlying.isEmpty() ? row->ticker.toUpper() : row->underlying;
    }
    const QString ticker = anchor.section(QLatin1Char('@'), 0, 0);
    if (!ticker.isEmpty() && ticker != anchor) {
        if (auto it = catalog.byTicker.constFind(ticker); it != catalog.byTicker.constEnd()) {
            const FinamSymbolRow* row = it.value();
            if (row != nullptr) return row->underlying.isEmpty() ? row->ticker.toUpper() : row->underlying;
        }
    }
    if (anchor.startsWith(QStringLiteral("SBER"))) return QStringLiteral("SBRF");
    return anchor.section(QLatin1Char('@'), 0, 0);
}

int finamSuggestionRank(const FinamSymbolRow& row,
                        const QString& venueMarket,
                        const QString& anchorMarket,
                        const QString& anchorUnderlying) {
    int rank = row.isArchived ? 120 : 100;
    if (venueMarket.trimmed().toLower() == QStringLiteral("futures") &&
        !anchorUnderlying.isEmpty() && row.underlying == anchorUnderlying) {
        rank = row.isArchived ? 20 : 0;
    }
    if (!anchorMarket.isEmpty() && anchorMarket.trimmed().toLower() == row.market) {
        rank += 20;
    }
    return rank;
}

QVariantMap finamSuggestionItem(const FinamSymbolRow& row, int rank) {
    QVariantMap item;
    item.insert(QStringLiteral("symbol"), row.symbol);
    item.insert(QStringLiteral("label"), row.name.isEmpty() ? row.symbol : row.symbol + QStringLiteral("  ") + row.name);
    QStringList detailParts{
        row.type,
        row.mic,
        row.ticker,
    };
    if (!row.underlying.isEmpty()) detailParts.push_back(QStringLiteral("underlying ") + row.underlying);
    if (!row.expiration.isEmpty()) detailParts.push_back(QStringLiteral("exp ") + row.expiration);
    if (!row.contractSize.isEmpty()) detailParts.push_back(QStringLiteral("contract ") + row.contractSize);
    detailParts.push_back(row.isArchived ? QStringLiteral("archived") : QStringLiteral("active"));
    item.insert(QStringLiteral("detail"), detailParts.join(QStringLiteral(" / ")));
    item.insert(QStringLiteral("rank"), rank);
    item.insert(QStringLiteral("expiration"), row.expiration);
    item.insert(QStringLiteral("archived"), row.isArchived);
    item.insert(QStringLiteral("underlying"), row.underlying);
    item.insert(QStringLiteral("ticker"), row.ticker);
    return item;
}

}  // namespace hftrec::gui::detail
