#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#if defined(HFTREC_WITH_TRADER_RUNTIME) && HFTREC_WITH_TRADER_RUNTIME
#include "hft_trader/runtime/history/candles/CandleRequestLimits.hpp"
#endif

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTime>
#include <QVariantMap>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "core/tui/RecorderTuiSymbols.hpp"

namespace hftrec::gui::detail {

namespace {

constexpr int kDetailedCandlesMaxLimit = 1'000'000;
constexpr std::int64_t kNsPerMs = 1'000'000ll;
constexpr int kFinamSmartRetryDays = 7;

QString normalizeToken(QString token) {
    return token.trimmed();
}

struct VenueSpec {
    const char* key;
    const char* label;
    const char* exchange;
    const char* market;
};

constexpr VenueSpec kVenues[] = {
    {"binance_futures", "Binance Futures", "binance", "futures"},
    {"binance_spot", "Binance Spot", "binance", "spot"},
    {"bybit_futures", "Bybit Futures", "bybit", "futures"},
    {"bybit_spot", "Bybit Spot", "bybit", "spot"},
    {"kucoin_futures", "KuCoin Futures", "kucoin", "futures"},
    {"kucoin_spot", "KuCoin Spot", "kucoin", "spot"},
    {"gate_futures", "Gate Futures", "gate", "futures"},
    {"gate_spot", "Gate Spot", "gate", "spot"},
    {"bitget_futures", "Bitget Futures", "bitget", "futures"},
    {"bitget_spot", "Bitget Spot", "bitget", "spot"},
    {"aster_futures", "Aster Futures", "aster", "futures"},
    {"aster_spot", "Aster Spot", "aster", "spot"},
    {"okx_futures", "OKX Futures", "okx", "futures"},
    {"okx_spot", "OKX Spot", "okx", "spot"},
    {"finam_futures", "FINAM Futures", "finam", "futures"},
    {"finam_spot", "FINAM Spot", "finam", "spot"},
    {"mexc_spot", "MEXC Spot", "mexc", "spot"},
    {"mexc_futures", "MEXC Futures", "mexc", "futures"},
    {"xt_futures", "XT Futures", "xt", "futures"},
    {"xt_spot", "XT Spot", "xt", "spot"},
    {"bingx_futures", "BingX Futures", "bingx", "futures"},
    {"bingx_spot", "BingX Spot", "bingx", "spot"},
    {"bitmart_futures", "Bitmart Futures", "bitmart", "futures"},
    {"bitmart_spot", "Bitmart Spot", "bitmart", "spot"},
    {"toobit_futures", "Toobit Futures", "toobit", "futures"},
    {"toobit_spot", "Toobit Spot", "toobit", "spot"},
    {"htx_futures", "HTX Futures", "htx", "futures"},
    {"htx_spot", "HTX Spot", "htx", "spot"},
    {"phemex_futures", "Phemex Futures", "phemex", "futures"},
    {"phemex_spot", "Phemex Spot", "phemex", "spot"},
    {"hyperliquid_futures", "Hyperliquid Futures", "hyperliquid", "futures"},
};

std::vector<VenueSpec> selectedVenues(const QStringList& venueKeys) {
    std::vector<VenueSpec> venues;
    for (const auto& rawKey : venueKeys) {
        const auto key = rawKey.trimmed().toLower();
        for (const auto& venue : kVenues) {
            if (key == QString::fromLatin1(venue.key)) {
                venues.push_back(venue);
                break;
            }
        }
    }
    if (venues.empty()) venues.push_back(kVenues[0]);
    return venues;
}

qsizetype venueIndex(const QString& venueKey) noexcept {
    const auto key = venueKey.trimmed().toLower();
    for (qsizetype i = 0; i < static_cast<qsizetype>(std::size(kVenues)); ++i) {
        if (key == QString::fromLatin1(kVenues[i].key)) return i;
    }
    return -1;
}

const VenueSpec* venueByKey(const QString& venueKey) noexcept {
    const qsizetype idx = venueIndex(venueKey);
    return idx >= 0 ? &kVenues[idx] : nullptr;
}

QString symbolsTextForVenue(const VenueSpec& venue,
                            const QStringList& venueSymbolsTexts,
                            const QString& fallbackSymbolsText) {
    const qsizetype idx = venueIndex(QString::fromLatin1(venue.key));
    if (idx >= 0 && idx < venueSymbolsTexts.size()) {
        return venueSymbolsTexts[idx].trimmed();
    }
    return venueSymbolsFromGlobalInput(QString::fromLatin1(venue.key), fallbackSymbolsText);
}

bool isFinamVenue(const VenueSpec& venue) noexcept {
    return std::string_view{venue.exchange} == "finam";
}

QString detailedCandlesTimeframeForVenue(const VenueSpec& venue, const QString& requestedTimeframe);

#if defined(HFTREC_WITH_TRADER_RUNTIME) && HFTREC_WITH_TRADER_RUNTIME
bool candleRequestLimitFor(const VenueSpec& venue,
                           const QString& timeframe,
                           hft_trader::runtime::candles::CandleRequestLimit* out) noexcept {
    const QString normalized = detailedCandlesTimeframeForVenue(venue, timeframe);
    const std::string tf = normalized.toStdString();
    return hft_trader::runtime::candles::findCandleRequestLimitByName(
        std::string_view(venue.exchange),
        std::string_view(venue.market),
        std::string_view(tf.data(), tf.size()),
        out);
}
#endif

QString pagingLabelFor(const char* paging) {
    if (paging == nullptr) return {};
    const QString value = QString::fromLatin1(paging);
    if (value == QStringLiteral("date_range")) return QStringLiteral("date range");
    if (value == QStringLiteral("cursor")) return QStringLiteral("cursor");
    if (value == QStringLiteral("limit")) return QStringLiteral("limit");
    return value;
}

bool isFinamVenueKey(const QString& venueKey) noexcept {
    const auto* venue = venueByKey(venueKey);
    return venue != nullptr && isFinamVenue(*venue);
}

QString formatUtcNs(std::int64_t ns) {
    if (ns <= 0) return QStringLiteral("now");
    return QDateTime::fromMSecsSinceEpoch(ns / kNsPerMs, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss'Z'"));
}

std::int64_t dateTimeToNs(const QDate& date, const QTime& time) {
    QDateTime dt(date, time);
    dt.setTimeSpec(Qt::UTC);
    return static_cast<std::int64_t>(dt.toMSecsSinceEpoch()) * kNsPerMs;
}

QDate previousWeekday(QDate date) {
    while (date.dayOfWeek() == 6 || date.dayOfWeek() == 7) date = date.addDays(-1);
    return date;
}

QDate previousTradingDate(QDate date) {
    return previousWeekday(date.addDays(-1));
}

QString normalizeDetailedCandlesEndMode(QString mode) {
    mode = mode.trimmed().toLower();
    if (mode == QStringLiteral("manual") || mode == QStringLiteral("manual_utc") ||
        mode == QStringLiteral("manual-utc")) {
        return QStringLiteral("manual_utc");
    }
    if (mode == QStringLiteral("now")) return QStringLiteral("now");
    return QStringLiteral("smart");
}

bool supportsDetailedCandlesVenue(const VenueSpec& venue) noexcept {
    const std::string_view exchange{venue.exchange};
    if (exchange == "hyperliquid") return false;
    if (exchange == "mexc") {
        return venue.market[0] == 's' && venue.market[1] == 'p' && venue.market[2] == 'o' && venue.market[3] == 't';
    }
    return true;
}

QString normalizeDetailedTimeframe(QString timeframe) {
    timeframe = timeframe.trimmed();
    if (timeframe == QStringLiteral("1M")) return timeframe;
    return timeframe.toLower();
}

QStringList detailedCandlesTimeframesForVenue(const VenueSpec& venue) {
    if (isFinamVenue(venue)) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("2h"),
            QStringLiteral("4h"),
            QStringLiteral("8h"),
            QStringLiteral("1d"),
            QStringLiteral("1w"),
            QStringLiteral("1M"),
        };
    }
    const QString exchange = QString::fromLatin1(venue.exchange);
    if (exchange == QStringLiteral("okx")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("3m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("2h"),
            QStringLiteral("4h"),
            QStringLiteral("6h"),
            QStringLiteral("12h"),
            QStringLiteral("1d"),
            QStringLiteral("1w"),
            QStringLiteral("1M"),
        };
    }
    if (exchange == QStringLiteral("kucoin")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("15m"),
            QStringLiteral("1h"),
            QStringLiteral("4h"),
            QStringLiteral("1d"),
        };
    }
    if (exchange == QStringLiteral("bitget")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("3m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("4h"),
            QStringLiteral("6h"),
            QStringLiteral("12h"),
            QStringLiteral("1d"),
        };
    }
    if (exchange == QStringLiteral("gate")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("4h"),
            QStringLiteral("8h"),
            QStringLiteral("1d"),
            QStringLiteral("7d"),
            QStringLiteral("30d"),
        };
    }
    return {
        QStringLiteral("1m"),
        QStringLiteral("3m"),
        QStringLiteral("5m"),
        QStringLiteral("15m"),
        QStringLiteral("30m"),
        QStringLiteral("1h"),
        QStringLiteral("2h"),
        QStringLiteral("4h"),
        QStringLiteral("6h"),
        QStringLiteral("8h"),
        QStringLiteral("12h"),
        QStringLiteral("1d"),
        QStringLiteral("3d"),
        QStringLiteral("1w"),
        QStringLiteral("1M"),
    };
}

bool detailedCandlesSupportsTimeframe(const VenueSpec& venue, const QString& timeframe) {
    return detailedCandlesTimeframesForVenue(venue).contains(timeframe);
}

QString detailedCandlesTimeframeSummary(const VenueSpec& venue) {
    return detailedCandlesTimeframesForVenue(venue).join(QStringLiteral(", "));
}

QString detailedCandlesTimeframeForVenue(const VenueSpec& venue, const QString& requestedTimeframe) {
    (void)venue;
    return normalizeDetailedTimeframe(requestedTimeframe);
}

struct ParsedSymbol {
    QString base;
    QString quote;
};

ParsedSymbol parseGlobalSymbol(QString symbol) {
    symbol = symbol.trimmed().toUpper();
    const QString swapMarker = QStringLiteral("-SWAP-");
    const auto swapMarkerIndex = symbol.indexOf(swapMarker);
    if (swapMarkerIndex >= 0) {
        const QString base = symbol.left(swapMarkerIndex);
        const QString quote = symbol.mid(swapMarkerIndex + swapMarker.size());
        if (!base.isEmpty() && !quote.isEmpty()) {
            return {base, quote};
        }
    }
    const QString suffix = QStringLiteral("-SWAP");
    if (symbol.endsWith(suffix) && symbol.size() > suffix.size() + 1u) {
        const auto sepPos = symbol.lastIndexOf(QLatin1Char('-'));
        if (sepPos > 0u && sepPos < (symbol.size() - suffix.size())) {
            const QString base = symbol.left(sepPos);
            const QString quote = symbol.mid(sepPos + 1u, symbol.size() - sepPos - suffix.size() - 1u);
            if (!base.isEmpty() && !quote.isEmpty()) {
                return {base, quote};
            }
        }
    }
    symbol.remove(QLatin1Char('-'));
    symbol.remove(QLatin1Char('_'));
    if (symbol.endsWith(QStringLiteral("SWAP"))) symbol.chop(4);

    static const QStringList quotes{
        QStringLiteral("USDT"),
        QStringLiteral("USDC"),
        QStringLiteral("USD"),
    };
    for (const auto& quote : quotes) {
        if (symbol.size() > quote.size() && symbol.endsWith(quote)) {
            symbol.chop(quote.size());
            return {symbol, quote};
        }
    }
    return {symbol, QStringLiteral("USDT")};
}

QString formattedVenueSymbol(const VenueSpec& venue, const ParsedSymbol& symbol);

QString marketDsl(const VenueSpec& venue) {
    const QString market = QString::fromLatin1(venue.market);
    if (market == QStringLiteral("spot")) return QStringLiteral("spot");
    if (market == QStringLiteral("margin")) return QStringLiteral("margin");
    if (market == QStringLiteral("inverse")) return QStringLiteral("inverse");
    if (market == QStringLiteral("swap")) return QStringLiteral("swap");
    return QStringLiteral("futures");
}

QString toDslSymbol(const VenueSpec& venue, const std::string& symbol) {
    const QString value = QString::fromStdString(symbol).trimmed();
    if (value.isEmpty()) return value;
    if (isFinamVenue(venue)) return value;
    return formattedVenueSymbol(venue, parseGlobalSymbol(value));
}

QString formattedVenueSymbol(const VenueSpec& venue, const ParsedSymbol& symbol) {
    const QString exchange = QString::fromLatin1(venue.exchange);
    const QString market = QString::fromLatin1(venue.market);
    QString base = symbol.base;
    QString quote = symbol.quote;

    if (exchange == QStringLiteral("kucoin") && market == QStringLiteral("futures")
        && base == QStringLiteral("BTC")) {
        base = QStringLiteral("XBT");
    }
    if (market == QStringLiteral("inverse")) quote = QStringLiteral("USD");
    if (exchange == QStringLiteral("bitget") && market == QStringLiteral("swap")) {
        quote = QStringLiteral("USDC");
    }

    if (exchange == QStringLiteral("kucoin")) {
        if (market == QStringLiteral("futures")) return base + quote + QStringLiteral("M");
        return base + QLatin1Char('-') + quote;
    }
    if (exchange == QStringLiteral("xt")) {
        return base.toLower() + QLatin1Char('_') + quote.toLower();
    }
    if (exchange == QStringLiteral("bingx")) {
        return base + QLatin1Char('-') + quote;
    }
    if (exchange == QStringLiteral("gate")) return base + QLatin1Char('_') + quote;
    if (exchange == QStringLiteral("toobit")) {
        if (market == QStringLiteral("futures") || market == QStringLiteral("swap")) return base + QLatin1Char('-') + QStringLiteral("SWAP-") + quote;
        return base + quote;
    }
    if (exchange == QStringLiteral("htx")) {
        if (market == QStringLiteral("futures") || market == QStringLiteral("swap")) return base + QLatin1Char('-') + quote;
        return base.toLower() + quote.toLower();
    }
    if (exchange == QStringLiteral("phemex")) {
        if (market == QStringLiteral("futures") || market == QStringLiteral("swap")) return base + quote;
        return QStringLiteral("s") + base + quote;
    }
    if (exchange == QStringLiteral("hyperliquid")) return base;
    if (exchange == QStringLiteral("okx")) {
        const QString result = base + QLatin1Char('-') + quote;
        return market == QStringLiteral("futures") ? result + QStringLiteral("-SWAP") : result;
    }
    if (exchange == QStringLiteral("mexc") && market == QStringLiteral("futures")) {
        return base + QLatin1Char('_') + quote;
    }
    if (exchange == QStringLiteral("binance") && market == QStringLiteral("inverse")) {
        return base + quote + QStringLiteral("_PERP");
    }
    return base + quote;
}

bool containsAlias(const QStringList& aliases, const QString& alias) {
    return aliases.contains(alias, Qt::CaseInsensitive);
}

QStringList canonicalTradesAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("id"),
        QStringLiteral("isBuyerMaker"),
        QStringLiteral("firstTradeId"),
        QStringLiteral("lastTradeId"),
        QStringLiteral("quoteQty"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
    };
}

QStringList canonicalLiquidationsAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("avgPrice"),
        QStringLiteral("filledQty"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
        QStringLiteral("orderType"),
        QStringLiteral("timeInForce"),
        QStringLiteral("status"),
        QStringLiteral("sourceMode"),
        QStringLiteral("captureSeq"),
        QStringLiteral("ingestSeq"),
    };
}
QStringList canonicalBookTickerAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
    };
}

QStringList canonicalOrderbookAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
    };
}

QStringList requiredTradesAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
    };
}

QStringList requiredLiquidationsAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("avgPrice"),
        QStringLiteral("filledQty"),
    };
}
QStringList requiredBookTickerAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("timestamp"),
    };
}

QStringList requiredOrderbookAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
    };
}

QString channelObjectName(const QString& channel) {
    if (channel == QStringLiteral("liquidations")) return QStringLiteral("liquidation");
    return channel;
}
QString buildAliasesSuffix(const QStringList& availableAliases,
                           const QStringList& selectedAliases) {
    QStringList orderedSelected;
    orderedSelected.reserve(selectedAliases.size());
    for (const auto& alias : availableAliases) {
        if (selectedAliases.contains(alias, Qt::CaseInsensitive)) orderedSelected.push_back(alias);
    }
    if (orderedSelected.isEmpty()) return {};
    return QStringLiteral(".aliases(%1)").arg(orderedSelected.join(QStringLiteral(",")));
}

int normalizedApiSlot(int apiSlot) noexcept {
    if (apiSlot < 1) return 1;
    if (apiSlot > 255) return 255;
    return apiSlot;
}

struct FinamSymbolRow {
    QString symbol;
    QString ticker;
    QString name;
    QString mic;
    QString market;
    QString type;
    QString underlying;
    QString expiration;
    QString contractSize;
    bool isArchived{false};
};

struct FinamCatalog {
    std::vector<FinamSymbolRow> rows;
    std::vector<const FinamSymbolRow*> spotRows;
    std::vector<const FinamSymbolRow*> futuresRows;
    QHash<QString, const FinamSymbolRow*> bySymbol;
    QHash<QString, const FinamSymbolRow*> byTicker;
    QHash<QString, std::vector<const FinamSymbolRow*>> futuresByUnderlying;
};

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

const FinamCatalog& finamCatalog() {
    static const FinamCatalog catalog = loadFinamCatalog();
    return catalog;
}

const std::vector<FinamSymbolRow>& finamSymbolRows() {
    return finamCatalog().rows;
}

const std::vector<const FinamSymbolRow*>& finamRowsForMarket(const VenueSpec& venue) {
    static const std::vector<const FinamSymbolRow*> empty;
    const FinamCatalog& catalog = finamCatalog();
    const QString market = QString::fromLatin1(venue.market);
    if (market == QStringLiteral("spot")) return catalog.spotRows;
    if (market == QStringLiteral("futures")) return catalog.futuresRows;
    return empty;
}

bool finamRowMatchesMarket(const FinamSymbolRow& row, const VenueSpec& venue) {
    return row.market == QString::fromLatin1(venue.market);
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
        if (idx == 0 || !prev.isLetterOrNumber() || prev.isDigit() || prev == QLatin1Char('@') || next.isDigit()) {
            return true;
        }
        from = idx + 1;
    }
    return false;
}

bool textMatchesQuery(const FinamSymbolRow& row, const QString& query) {
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
    if (anchor.startsWith(QStringLiteral("SBER"))) return QStringLiteral("SBRF");
    return anchor.section(QLatin1Char('@'), 0, 0);
}

int finamSuggestionRank(const FinamSymbolRow& row,
                        const VenueSpec& venue,
                        const QString& anchorMarket,
                        const QString& anchorUnderlying) {
    int rank = row.isArchived ? 120 : 100;
    if (QString::fromLatin1(venue.market) == QStringLiteral("futures") &&
        !anchorUnderlying.isEmpty() && row.underlying == anchorUnderlying) {
        rank = row.isArchived ? 20 : 0;
    }
    if (!anchorMarket.isEmpty() && anchorMarket == row.market) {
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
    return item;
}

capture::CaptureConfig makeDetailedCandlesConfig(const QString& outputDirectory,
                                                 const QString& envPath,
                                                 int apiSlot,
                                                 const VenueSpec& venue,
                                                 const std::string& symbol,
                                                 const QString& timeframe,
                                                 int limit,
                                                 std::int64_t endNs) {
    capture::CaptureConfig config{};
    config.exchange = venue.exchange;
    config.market = venue.market;
    config.symbols = {symbol};
    config.envPath = std::filesystem::path{envPath.toStdString()};
    config.apiSlot = static_cast<std::uint8_t>(normalizedApiSlot(apiSlot));
    config.outputDir = std::filesystem::path{outputDirectory.toStdString()};
    config.detailedCandlesTimeframe = timeframe.toStdString();
    config.detailedCandlesLimit = static_cast<std::uint32_t>(std::clamp(limit, 1, kDetailedCandlesMaxLimit));
    config.detailedCandlesEndNs = endNs > 0 ? endNs : 0;
    return config;
}

bool appendDetailedCandlesConfig(std::vector<capture::CaptureConfig>& configs,
                                 const QString& outputDirectory,
                                 const QString& envPath,
                                 int apiSlot,
                                 const QString& venueKey,
                                 const QString& symbolText,
                                 const QString& timeframe,
                                 int limit,
                                 const QString& emptySymbolError,
                                 QString* errorText,
                                 std::int64_t endNs) {
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) {
        if (errorText != nullptr) *errorText = QStringLiteral("Select detailed candles venue");
        return false;
    }
    if (!supportsDetailedCandlesVenue(*venue)) {
        if (errorText != nullptr) *errorText = QStringLiteral("Selected venue does not support detailed candles");
        return false;
    }

    const QString tf = detailedCandlesTimeframeForVenue(*venue, timeframe);
    if (tf.isEmpty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter detailed candles timeframe");
        return false;
    }
    if (!detailedCandlesSupportsTimeframe(*venue, tf)) {
        if (errorText != nullptr) {
            *errorText = QStringLiteral("%1 candles support %2; got %3")
                .arg(QString::fromLatin1(venue->label), detailedCandlesTimeframeSummary(*venue), tf);
        }
        return false;
    }

    const auto symbols = normalizedSymbols(symbolText);
    if (symbols.empty()) {
        if (errorText != nullptr) *errorText = emptySymbolError;
        return false;
    }
    if (symbols.size() != 1u) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter one detailed candles symbol per row");
        return false;
    }

    configs.push_back(makeDetailedCandlesConfig(outputDirectory, envPath, apiSlot, *venue, symbols.front(), tf, limit, endNs));
    return true;
}

}  // namespace

QStringList loadAliasesForChannel(const char* channelName) {
    const auto channel = QString::fromUtf8(channelName);
    if (channel == QStringLiteral("trades")) return canonicalTradesAliases();
    if (channel == QStringLiteral("liquidations")) return canonicalLiquidationsAliases();
    if (channel == QStringLiteral("bookticker")) return canonicalBookTickerAliases();
    if (channel == QStringLiteral("orderbook")) return canonicalOrderbookAliases();
    return {};
}

QVariantList venueChoices() {
    QVariantList choices;
    for (const auto& venue : kVenues) {
        QVariantMap item;
        item.insert(QStringLiteral("key"), QString::fromLatin1(venue.key));
        item.insert(QStringLiteral("label"), QString::fromLatin1(venue.label));
        choices.push_back(item);
    }
    return choices;
}

QVariantList detailedCandlesVenueChoices() {
    QVariantList choices;
    for (const auto& venue : kVenues) {
        if (!supportsDetailedCandlesVenue(venue)) continue;
        QVariantMap item;
        item.insert(QStringLiteral("key"), QString::fromLatin1(venue.key));
        item.insert(QStringLiteral("label"), QString::fromLatin1(venue.label));
        choices.push_back(item);
    }
    return choices;
}

QStringList requiredAliasesForChannel(const QString& channel) {
    if (channel == QStringLiteral("trades")) return requiredTradesAliases();
    if (channel == QStringLiteral("liquidations")) return requiredLiquidationsAliases();
    if (channel == QStringLiteral("bookticker")) return requiredBookTickerAliases();
    if (channel == QStringLiteral("orderbook")) return requiredOrderbookAliases();
    return {};
}

bool isRequiredAliasForChannel(const QString& channel, const QString& alias) {
    return containsAlias(requiredAliasesForChannel(channel), alias);
}

QStringList normalizedSelectedAliasesForChannel(const QString& channel,
                                                const QStringList& selectedAliases) {
    QStringList normalized = selectedAliases;
    for (const auto& alias : requiredAliasesForChannel(channel)) {
        if (!normalized.contains(alias, Qt::CaseInsensitive)) normalized.push_back(alias);
    }
    return normalized;
}

int aliasWeightBytes(const QString& alias) {
    if (alias == QStringLiteral("price") || alias == QStringLiteral("amount") ||
        alias == QStringLiteral("quoteQty") || alias == QStringLiteral("avgPrice") ||
        alias == QStringLiteral("filledQty") || alias == QStringLiteral("timestamp") ||
        alias == QStringLiteral("id") || alias == QStringLiteral("firstTradeId") ||
        alias == QStringLiteral("lastTradeId") || alias == QStringLiteral("captureSeq") ||
        alias == QStringLiteral("ingestSeq") || alias == QStringLiteral("updateId") ||
        alias == QStringLiteral("bidPrice") || alias == QStringLiteral("bidQty") ||
        alias == QStringLiteral("askPrice") || alias == QStringLiteral("askQty")) {
        return 8;
    }
    if (alias == QStringLiteral("side") || alias == QStringLiteral("isBuyerMaker") ||
        alias == QStringLiteral("orderType") || alias == QStringLiteral("timeInForce") ||
        alias == QStringLiteral("status") || alias == QStringLiteral("sourceMode") ||
        alias == QStringLiteral("exchange") || alias == QStringLiteral("market")) {
        return 1;
    }
    if (alias == QStringLiteral("symbol")) return 32;
    return 0;
}

QString channelWeightSummary(const QString& channel, const QStringList& selectedAliases) {
    const auto normalizedAliases = normalizedSelectedAliasesForChannel(channel, selectedAliases);
    if (channel == QStringLiteral("orderbook")) {
        int baseBytes = 0;
        int perBidBytes = 0;
        int perAskBytes = 0;
        for (const auto& alias : normalizedAliases) {
            if (alias == QStringLiteral("timestamp") || alias == QStringLiteral("symbol") ||
                alias == QStringLiteral("exchange") || alias == QStringLiteral("market")) {
                baseBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("side")) {
                perBidBytes += aliasWeightBytes(alias);
                perAskBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("bidPrice") || alias == QStringLiteral("bidQty")) {
                perBidBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("askPrice") || alias == QStringLiteral("askQty")) {
                perAskBytes += aliasWeightBytes(alias);
            }
        }
        return QStringLiteral("Total ") + QString::number(baseBytes)
            + QStringLiteral("B + N_bid*") + QString::number(perBidBytes)
            + QStringLiteral("B + N_ask*") + QString::number(perAskBytes)
            + QStringLiteral("B");
    }

    int totalBytes = 0;
    for (const auto& alias : normalizedAliases) totalBytes += aliasWeightBytes(alias);
    return QStringLiteral("Total ") + QString::number(totalBytes) + QStringLiteral("B");
}

std::vector<std::string> normalizedSymbols(const QString& symbolsText) {
    std::vector<std::string> symbols;
    const auto rawTokens = symbolsText.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    symbols.reserve(rawTokens.size());
    for (const auto& rawToken : rawTokens) {
        const auto normalized = normalizeToken(rawToken);
        if (normalized.isEmpty()) continue;
        const auto asStd = normalized.toStdString();
        if (std::find(symbols.begin(), symbols.end(), asStd) == symbols.end()) symbols.push_back(asStd);
    }
    return symbols;
}

QString venueSymbolsFromGlobalInput(const QString& venueKey, const QString& symbolsText) {
    const qsizetype idx = venueIndex(venueKey);
    if (idx < 0) return {};

    if (isFinamVenue(kVenues[idx])) {
        QStringList formatted;
        const auto symbols = normalizedSymbols(symbolsText);
        formatted.reserve(static_cast<qsizetype>(symbols.size()));
        for (const auto& symbol : symbols) {
            const QString value = QString::fromStdString(symbol).trimmed();
            if (!value.isEmpty()) formatted.push_back(value);
        }
        return formatted.join(QLatin1Char('\n'));
    }
    return QString::fromStdString(
        hftrec::tui::venueSymbolsFromGlobalInput(venueKey.toStdString(), symbolsText.toStdString()));
}

QString venueSymbolPlaceholder(const QString& venueKey) {
    const qsizetype idx = venueIndex(venueKey);
    if (idx < 0) return QStringLiteral("Example: BTCUSDT");
    if (isFinamVenue(kVenues[idx])) return QStringLiteral("Example: SBER@MISX");
    return QStringLiteral("Example: %1")
        .arg(QString::fromStdString(hftrec::tui::venueSymbolsFromGlobalInput(venueKey.toStdString(), "BTCUSDT")));
}

QVariantList detailedCandlesEndModeChoices() {
    QVariantList choices;
    auto append = [&](const QString& label, const QString& value) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), label);
        item.insert(QStringLiteral("value"), value);
        choices.push_back(item);
    };
    append(QStringLiteral("Smart"), QStringLiteral("smart"));
    append(QStringLiteral("Now"), QStringLiteral("now"));
    append(QStringLiteral("Manual UTC"), QStringLiteral("manual_utc"));
    return choices;
}

std::int64_t parseDetailedCandlesEndUtcText(const QString& text, QString* errorText) {
    if (errorText != nullptr) errorText->clear();
    QString normalized = text.trimmed();
    if (normalized.isEmpty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter manual UTC end time");
        return 0;
    }
    if (normalized.endsWith(QStringLiteral(" UTC"), Qt::CaseInsensitive)) {
        normalized.chop(4);
        normalized += QStringLiteral("Z");
    }

    QDateTime dt = QDateTime::fromString(normalized, Qt::ISODate);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(normalized, QStringLiteral("yyyy-MM-dd HH:mm:ss'Z'"));
        if (dt.isValid()) dt.setTimeSpec(Qt::UTC);
    }
    if (!dt.isValid()) {
        dt = QDateTime::fromString(normalized, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (dt.isValid()) dt.setTimeSpec(Qt::UTC);
    }
    if (!dt.isValid()) {
        if (errorText != nullptr) {
            *errorText = QStringLiteral("End time must be UTC, e.g. 2026-06-19 20:45:00Z");
        }
        return 0;
    }
    dt = dt.toUTC();
    const qint64 ms = dt.toMSecsSinceEpoch();
    if (ms <= 0) {
        if (errorText != nullptr) *errorText = QStringLiteral("End time must be after Unix epoch");
        return 0;
    }
    return static_cast<std::int64_t>(ms) * kNsPerMs;
}

std::vector<std::int64_t> detailedCandlesEndCandidatesNs(const QString& mode,
                                                         const QString& manualUtcText,
                                                         const QString& leg1VenueKey,
                                                         const QString& leg2VenueKey,
                                                         std::int64_t nowNs,
                                                         QString* resolvedText,
                                                         QString* errorText) {
    if (resolvedText != nullptr) resolvedText->clear();
    if (errorText != nullptr) errorText->clear();

    const QString normalizedMode = normalizeDetailedCandlesEndMode(mode);
    if (normalizedMode == QStringLiteral("now")) {
        if (resolvedText != nullptr) *resolvedText = QStringLiteral("now");
        return {0};
    }
    if (normalizedMode == QStringLiteral("manual_utc")) {
        const std::int64_t endNs = parseDetailedCandlesEndUtcText(manualUtcText, errorText);
        if (endNs <= 0) return {};
        if (resolvedText != nullptr) *resolvedText = formatUtcNs(endNs);
        return {endNs};
    }

    const bool finam = isFinamVenueKey(leg1VenueKey) || isFinamVenueKey(leg2VenueKey);
    if (!finam) {
        if (resolvedText != nullptr) *resolvedText = QStringLiteral("now");
        return {0};
    }

    const std::int64_t safeNowNs = nowNs > 0
        ? nowNs
        : static_cast<std::int64_t>(QDateTime::currentMSecsSinceEpoch()) * kNsPerMs;
    const QDateTime now = QDateTime::fromMSecsSinceEpoch(safeNowNs / kNsPerMs, Qt::UTC);
    const QTime finamClose(20, 45, 0);
    QDate anchorDate = previousWeekday(now.date());
    std::int64_t firstEndNs = 0;
    if (now.date().dayOfWeek() >= 1 && now.date().dayOfWeek() <= 5 && now.time() < finamClose) {
        firstEndNs = (safeNowNs / 1'000'000'000ll) * 1'000'000'000ll;
    } else {
        firstEndNs = dateTimeToNs(anchorDate, finamClose);
    }

    std::vector<std::int64_t> out;
    out.reserve(kFinamSmartRetryDays);
    out.push_back(firstEndNs);
    QDate cursor = QDateTime::fromMSecsSinceEpoch(firstEndNs / kNsPerMs, Qt::UTC).date();
    while (static_cast<int>(out.size()) < kFinamSmartRetryDays) {
        cursor = previousTradingDate(cursor);
        out.push_back(dateTimeToNs(cursor, finamClose));
    }

    if (resolvedText != nullptr && !out.empty()) {
        *resolvedText = QStringLiteral("%1 smart candidates from %2")
            .arg(QString::number(out.size()), formatUtcNs(out.front()));
    }
    return out;
}

QString venueSymbolExample(const VenueSpec& venue) {
    if (isFinamVenue(venue)) return QStringLiteral("SBER@MISX");
    return QString::fromStdString(hftrec::tui::venueSymbolsFromGlobalInput(venue.key, "BTCUSDT"));
}

QString missingVenueSymbolsText(const QStringList& venueKeys, const QStringList& venueSymbolsTexts) {
    QStringList missing;
    for (const auto& rawKey : venueKeys) {
        const auto key = rawKey.trimmed().toLower();
        const qsizetype idx = venueIndex(key);
        if (idx < 0) continue;
        if (idx < venueSymbolsTexts.size() && !venueSymbolsTexts[idx].trimmed().isEmpty()) continue;
        const auto label = QString::fromLatin1(kVenues[idx].label);
        const auto example = venueSymbolExample(kVenues[idx]);
        missing.push_back(QStringLiteral("%1 symbol is empty; expected %2").arg(label, example));
    }
    return missing.join(QStringLiteral(" | "));
}

QString buildRequestPreview(const QString& channel,
                            const QStringList& availableAliases,
                            const QStringList& selectedAliases,
                            const QStringList& venueKeys,
                            const QStringList& venueSymbolsTexts,
                            const QString& symbolsText,
                            int apiSlot) {
    const auto normalizedSelectedAliases = normalizedSelectedAliasesForChannel(channel, selectedAliases);
    const auto aliasesSuffix = buildAliasesSuffix(availableAliases, normalizedSelectedAliases);
    const auto objectName = channelObjectName(channel);
    const auto apiSuffix = QStringLiteral(".api(%1)").arg(normalizedApiSlot(apiSlot));

    const auto venues = selectedVenues(venueKeys);

    QStringList commands;
    for (const auto& venue : venues) {
        const auto symbols = normalizedSymbols(symbolsTextForVenue(venue, venueSymbolsTexts, symbolsText));
        if (symbols.empty()) {
            commands.push_back(QStringLiteral("subscribe().object(%1).exchange(%2).market(%3)%4.symbol(<symbol>)%5")
                                   .arg(objectName, QString::fromLatin1(venue.exchange), marketDsl(venue), apiSuffix, aliasesSuffix));
            continue;
        }
        for (const auto& symbol : symbols) {
            commands.push_back(
                QStringLiteral("subscribe().object(%1).exchange(%2).market(%3)%4.symbol(%5)%6")
                    .arg(objectName,
                         QString::fromLatin1(venue.exchange),
                         marketDsl(venue),
                         apiSuffix,
                         toDslSymbol(venue, symbol),
                         aliasesSuffix));
        }
    }
    return commands.join(QStringLiteral("\n"));
}

std::vector<capture::CaptureConfig> makeConfigs(const QString& outputDirectory,
                                                const QString& envPath,
                                                int apiSlot,
                                                const QStringList& venueKeys,
                                                const QStringList& venueSymbolsTexts,
                                                const QString& symbolsText,
                                                const QStringList& tradesAvailableAliases,
                                                const QStringList& liquidationsAvailableAliases,
                                                const QStringList& bookTickerAvailableAliases,
                                                const QStringList& orderbookAvailableAliases,
                                                const QStringList& selectedTradesAliases,
                                                const QStringList& selectedLiquidationsAliases,
                                                const QStringList& selectedBookTickerAliases,
                                                const QStringList& selectedOrderbookAliases,
                                                int tradesHistoryWarmupSec) {
    std::vector<capture::CaptureConfig> configs;
    const auto venues = selectedVenues(venueKeys);

    const auto normalizedTradesAliases = normalizedSelectedAliasesForChannel(QStringLiteral("trades"), selectedTradesAliases);
    const auto normalizedLiquidationsAliases = normalizedSelectedAliasesForChannel(QStringLiteral("liquidations"), selectedLiquidationsAliases);
    const auto normalizedBookTickerAliases = normalizedSelectedAliasesForChannel(QStringLiteral("bookticker"), selectedBookTickerAliases);
    const auto normalizedOrderbookAliases = normalizedSelectedAliasesForChannel(QStringLiteral("orderbook"), selectedOrderbookAliases);
    const auto tradesSuffix = buildAliasesSuffix(tradesAvailableAliases, normalizedTradesAliases);
    const auto liquidationsSuffix = buildAliasesSuffix(liquidationsAvailableAliases, normalizedLiquidationsAliases);
    const auto bookTickerSuffix = buildAliasesSuffix(bookTickerAvailableAliases, normalizedBookTickerAliases);
    const auto orderbookSuffix = buildAliasesSuffix(orderbookAvailableAliases, normalizedOrderbookAliases);

    for (const auto& venue : venues) {
        const auto symbols = normalizedSymbols(symbolsTextForVenue(venue, venueSymbolsTexts, symbolsText));
        for (const auto& symbol : symbols) {
            capture::CaptureConfig config{};
            config.exchange = venue.exchange;
            config.market = venue.market;
            config.symbols = {symbol};
            config.envPath = std::filesystem::path{envPath.toStdString()};
            config.apiSlot = static_cast<std::uint8_t>(normalizedApiSlot(apiSlot));
            config.outputDir = std::filesystem::path{outputDirectory.toStdString()};
            config.durationSec = 0;
            config.snapshotIntervalSec = 60;
            int effectiveTradesHistoryWarmupSec = tradesHistoryWarmupSec;
            if ((venue.exchange[0] == 'g' || venue.exchange[0] == 'k') && effectiveTradesHistoryWarmupSec > 60) {
                effectiveTradesHistoryWarmupSec = 60;
            }
            config.tradesHistoryWarmupSec = effectiveTradesHistoryWarmupSec;

            const auto symbolDsl = toDslSymbol(venue, symbol);
            config.tradesAliases.reserve(static_cast<std::size_t>(normalizedTradesAliases.size()));
            for (const auto& alias : normalizedTradesAliases) config.tradesAliases.push_back(alias.toStdString());
            config.liquidationAliases.reserve(static_cast<std::size_t>(normalizedLiquidationsAliases.size()));
            for (const auto& alias : normalizedLiquidationsAliases) config.liquidationAliases.push_back(alias.toStdString());
            config.bookTickerAliases.reserve(static_cast<std::size_t>(normalizedBookTickerAliases.size()));
            for (const auto& alias : normalizedBookTickerAliases) config.bookTickerAliases.push_back(alias.toStdString());
            config.orderbookAliases.reserve(static_cast<std::size_t>(normalizedOrderbookAliases.size()));
            for (const auto& alias : normalizedOrderbookAliases) config.orderbookAliases.push_back(alias.toStdString());

            const auto exchangeDsl = QString::fromLatin1(venue.exchange);
            const auto market = marketDsl(venue);
            const auto apiSlotText = QString::number(normalizedApiSlot(apiSlot));
            config.tradesRequestCommand =
                QStringLiteral("subscribe().object(trades).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, tradesSuffix)
                    .toStdString();
            config.liquidationRequestCommand =
                QStringLiteral("subscribe().object(liquidation).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, liquidationsSuffix)
                    .toStdString();
            config.bookTickerRequestCommand =
                QStringLiteral("subscribe().object(bookticker).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, bookTickerSuffix)
                    .toStdString();
            config.orderbookRequestCommand =
                QStringLiteral("subscribe().object(orderbook).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, orderbookSuffix)
                    .toStdString();
            configs.push_back(std::move(config));
        }
    }

    return configs;
}

std::vector<capture::CaptureConfig> makeDetailedCandlesConfigs(const QString& outputDirectory,
                                                               const QString& envPath,
                                                               int apiSlot,
                                                               const QString& venueKey,
                                                               const QString& symbolText,
                                                               const QString& timeframe,
                                                               int limit,
                                                               QString* errorText,
                                                               std::int64_t endNs) {
    if (errorText != nullptr) errorText->clear();
    std::vector<capture::CaptureConfig> configs;
    if (!appendDetailedCandlesConfig(configs,
                                     outputDirectory,
                                     envPath,
                                     apiSlot,
                                     venueKey,
                                     symbolText,
                                     timeframe,
                                     limit,
                                     QStringLiteral("Enter detailed candles symbol"),
                                     errorText,
                                     endNs)) {
        if (errorText != nullptr && *errorText == QStringLiteral("Enter one detailed candles symbol per row")) {
            *errorText = QStringLiteral("Enter one detailed candles symbol");
        }
        return configs;
    }
    return configs;
}

std::vector<capture::CaptureConfig> makeDetailedCandlesConfigs(const QString& outputDirectory,
                                                               const QString& envPath,
                                                               int apiSlot,
                                                               const QString& leg1VenueKey,
                                                               const QString& leg1SymbolText,
                                                               const QString& leg2VenueKey,
                                                               const QString& leg2SymbolText,
                                                               const QString& timeframe,
                                                               int limit,
                                                               QString* errorText,
                                                               std::int64_t endNs) {
    if (errorText != nullptr) errorText->clear();
    std::vector<capture::CaptureConfig> configs;

    const bool leg1Filled = !leg1SymbolText.trimmed().isEmpty();
    const bool leg2Filled = !leg2SymbolText.trimmed().isEmpty();
    if (!leg1Filled && leg2Filled) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter first detailed candles symbol");
        return configs;
    }
    if (!leg1Filled) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter detailed candles symbol");
        return configs;
    }

    if (!appendDetailedCandlesConfig(configs,
                                     outputDirectory,
                                     envPath,
                                     apiSlot,
                                     leg1VenueKey,
                                     leg1SymbolText,
                                     timeframe,
                                     limit,
                                     QStringLiteral("Enter detailed candles symbol"),
                                     errorText,
                                     endNs)) {
        return {};
    }
    if (leg2Filled && !appendDetailedCandlesConfig(configs,
                                                   outputDirectory,
                                                   envPath,
                                                   apiSlot,
                                                   leg2VenueKey,
                                                   leg2SymbolText,
                                                   timeframe,
                                                   limit,
                                                   QStringLiteral("Enter second detailed candles symbol"),
                                                   errorText,
                                                   endNs)) {
        return {};
    }
    return configs;
}

QVariantList detailedCandlesSymbolSuggestions(const QString& venueKey,
                                              const QString& query,
                                              const QString& anchorVenueKey,
                                              const QString& anchorSymbolText) {
    QVariantList result;
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) return result;

    if (isFinamVenue(*venue)) {
        std::vector<QVariantMap> rows;
        const QString normalizedQuery = query.trimmed();
        const QString anchorUnderlying = finamAnchorUnderlying(anchorSymbolText);
        const auto* anchorVenue = venueByKey(anchorVenueKey);
        const QString anchorMarket = anchorVenue == nullptr ? QString{} : QString::fromLatin1(anchorVenue->market);
        const auto& marketRows = finamRowsForMarket(*venue);
        const std::vector<const FinamSymbolRow*>* scanRows = &marketRows;
        const bool preferUnderlyingFutures = QString::fromLatin1(venue->market) == QStringLiteral("futures") &&
                                            !anchorUnderlying.isEmpty();
        if (preferUnderlyingFutures) {
            const FinamCatalog& catalog = finamCatalog();
            auto it = catalog.futuresByUnderlying.constFind(anchorUnderlying);
            if (it != catalog.futuresByUnderlying.constEnd() && !it.value().empty()) scanRows = &it.value();
        }
        auto collectRows = [&](const std::vector<const FinamSymbolRow*>& sourceRows) {
            for (const FinamSymbolRow* rowPtr : sourceRows) {
                if (rowPtr == nullptr) continue;
                const FinamSymbolRow& row = *rowPtr;
                if (!finamRowMatchesMarket(row, *venue)) continue;
                if (!textMatchesQuery(row, normalizedQuery)) continue;
                rows.push_back(finamSuggestionItem(row, finamSuggestionRank(row, *venue, anchorMarket, anchorUnderlying)));
            }
        };
        collectRows(*scanRows);
        if (rows.empty() && scanRows != &marketRows && !normalizedQuery.isEmpty()) collectRows(marketRows);
        std::stable_sort(rows.begin(), rows.end(), [](const QVariantMap& lhs, const QVariantMap& rhs) {
            const int leftRank = lhs.value(QStringLiteral("rank")).toInt();
            const int rightRank = rhs.value(QStringLiteral("rank")).toInt();
            if (leftRank != rightRank) return leftRank < rightRank;
            const QString leftExpiration = lhs.value(QStringLiteral("expiration")).toString();
            const QString rightExpiration = rhs.value(QStringLiteral("expiration")).toString();
            if (leftExpiration != rightExpiration) return leftExpiration > rightExpiration;
            return lhs.value(QStringLiteral("symbol")).toString() < rhs.value(QStringLiteral("symbol")).toString();
        });
        const qsizetype maxSuggestions = 200;
        for (const auto& row : rows) {
            if (result.size() >= maxSuggestions) break;
            result.push_back(row);
        }
        return result;
    }

    const QString source = query.trimmed().isEmpty() ? anchorSymbolText : query;
    const QString formatted = venueSymbolsFromGlobalInput(venueKey, source).trimmed();
    const auto symbols = normalizedSymbols(formatted);
    if (symbols.empty()) return result;

    const QString symbol = QString::fromStdString(symbols.front()).trimmed();
    QVariantMap item;
    item.insert(QStringLiteral("symbol"), symbol);
    item.insert(QStringLiteral("label"), symbol);
    item.insert(QStringLiteral("detail"), QStringLiteral("Derived for %1").arg(QString::fromLatin1(venue->label)));
    item.insert(QStringLiteral("rank"), 0);
    result.push_back(item);
    return result;
}

QVariantList detailedCandlesTimeframeChoices(const QString& venueKey) {
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) venue = &kVenues[0];
    QVariantList choices;
    const auto timeframes = detailedCandlesTimeframesForVenue(*venue);
    for (const auto& timeframe : timeframes) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), timeframe);
        item.insert(QStringLiteral("value"), timeframe);
#if defined(HFTREC_WITH_TRADER_RUNTIME) && HFTREC_WITH_TRADER_RUNTIME
        hft_trader::runtime::candles::CandleRequestLimit limit{};
        if (candleRequestLimitFor(*venue, timeframe, &limit) && limit.maxCandlesPerRequest != 0u) {
            item.insert(QStringLiteral("rightText"),
                        QStringLiteral("page %1").arg(QString::number(limit.maxCandlesPerRequest)));
        } else
#endif
        {
            item.insert(QStringLiteral("rightText"), isFinamVenue(*venue)
                ? QStringLiteral("FINAM bars")
                : QStringLiteral("REST pages"));
        }
        choices.push_back(item);
    }
    return choices;
}

QString defaultDetailedCandlesTimeframe(const QString& venueKey) {
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) venue = &kVenues[0];
    const auto timeframes = detailedCandlesTimeframesForVenue(*venue);
    return timeframes.isEmpty() ? QStringLiteral("1m") : timeframes.front();
}

QString detailedCandlesLimitHint(const QString& venueKey,
                                 const QString& timeframe) {
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) return QStringLiteral("Total candles");
#if defined(HFTREC_WITH_TRADER_RUNTIME) && HFTREC_WITH_TRADER_RUNTIME
    hft_trader::runtime::candles::CandleRequestLimit requestLimit{};
    if (candleRequestLimitFor(*venue, timeframe, &requestLimit) && requestLimit.maxCandlesPerRequest != 0u) {
        return QStringLiteral("Page max: %1").arg(QString::number(requestLimit.maxCandlesPerRequest));
    }
#endif
    return QStringLiteral("Total candles");
}

QString detailedCandlesLimitWarning(const QString& venueKey,
                                    const QString& timeframe,
                                    int limit) {
    const auto* venue = venueByKey(venueKey);
    const QString totalText = QString::number(std::clamp(limit, 1, kDetailedCandlesMaxLimit));
    if (venue == nullptr) {
        return QStringLiteral("Total target: %1 candles. Paging stops when the exchange returns no older candles.").arg(totalText);
    }
#if defined(HFTREC_WITH_TRADER_RUNTIME) && HFTREC_WITH_TRADER_RUNTIME
    hft_trader::runtime::candles::CandleRequestLimit requestLimit{};
    if (candleRequestLimitFor(*venue, timeframe, &requestLimit) && requestLimit.maxCandlesPerRequest != 0u) {
        const QString paging = pagingLabelFor(hft_trader::runtime::candles::candleRequestPagingName(requestLimit.paging));
        const QString total = requestLimit.maxTotalUnbounded
            ? QStringLiteral("paged until older candles end")
            : QStringLiteral("max total %1").arg(QString::number(requestLimit.maxTotalCandles));
        return QStringLiteral("Total target: %1 candles. Max per REST page for %2/%3/%4: %5; paging: %6, %7. Written rows can be lower than requested.")
            .arg(totalText,
                 QString::fromLatin1(venue->exchange),
                 QString::fromLatin1(venue->market),
                 detailedCandlesTimeframeForVenue(*venue, timeframe),
                 QString::number(requestLimit.maxCandlesPerRequest),
                 paging,
                 total);
    }
#endif
    return QStringLiteral("Total target: %1 candles. No exchange page limit metadata yet; paging stops when the exchange returns no older candles.").arg(totalText);
}

QString buildDetailedCandlesPreview(const QString& venueKey,
                                    const QString& symbolText,
                                    const QString& timeframe,
                                    int limit) {
    const auto* venue = venueByKey(venueKey);
    const QString tf = venue == nullptr ? normalizeDetailedTimeframe(timeframe) : detailedCandlesTimeframeForVenue(*venue, timeframe);
    const QString limitText = QString::number(std::clamp(limit, 1, kDetailedCandlesMaxLimit));
    if (venue == nullptr) {
        return QStringLiteral("get().object(klines).exchange(<venue>).market(<market>).timeframe(%1).limit(%2).symbol(<symbol>) -> jsonl/candles2_%1.jsonl")
            .arg(tf, limitText);
    }
    const auto symbols = normalizedSymbols(symbolText);
    const QString exchange = QString::fromLatin1(venue->exchange);
    const QString market = marketDsl(*venue);
    const QString path = QStringLiteral("jsonl/candles2_%1.jsonl").arg(tf);
    const QString symbol = symbols.empty()
        ? QStringLiteral("<symbol>")
        : toDslSymbol(*venue, symbols.front());
    QString command = QStringLiteral("get().object(klines).exchange(%1).market(%2).timeframe(%3).limit(%4).symbol(%5)")
        .arg(exchange, market, tf, limitText, symbol);
    return command + QStringLiteral(" -> ") + path;
}

QString buildDetailedCandlesPreview(const QString& leg1VenueKey,
                                    const QString& leg1SymbolText,
                                    const QString& leg2VenueKey,
                                    const QString& leg2SymbolText,
                                    const QString& timeframe,
                                    int limit) {
    QStringList previews;
    previews.push_back(buildDetailedCandlesPreview(leg1VenueKey, leg1SymbolText, timeframe, limit));
    if (!leg2SymbolText.trimmed().isEmpty()) {
        previews.push_back(buildDetailedCandlesPreview(leg2VenueKey, leg2SymbolText, timeframe, limit));
    }
    return previews.join(QStringLiteral("\n"));
}

}  // namespace hftrec::gui::detail
