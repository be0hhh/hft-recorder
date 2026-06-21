#include "gui/models/ViewerSourceListModel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "gui/models/BacktestSessionSummary.hpp"
#include "gui/viewmodels/CaptureViewModel.hpp"

namespace hftrec::gui {

namespace {

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString displayExchange(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return QStringLiteral("Unknown Exchange");
    value.replace('_', ' ');
    value.replace('-', ' ');
    QStringList parts = value.split(' ', Qt::SkipEmptyParts);
    for (QString& part : parts) {
        if (part.size() <= 3) part = part.toUpper();
        else part = part.left(1).toUpper() + part.mid(1).toLower();
    }
    return parts.join(' ');
}

QString displayMarket(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return QStringLiteral("Unknown Market");
    value.replace('_', ' ');
    value.replace('-', ' ');
    QStringList parts = value.split(' ', Qt::SkipEmptyParts);
    for (QString& part : parts) {
        part = part.left(1).toUpper() + part.mid(1).toLower();
    }
    return parts.join(' ');
}

QString buildLiveLabel(const QString& exchange, const QString& market, const QString& symbol) {
    return QStringLiteral("LIVE | %1 | %2 | %3")
        .arg(displayExchange(exchange),
             displayMarket(market),
             symbol.trimmed().isEmpty() ? QStringLiteral("Unknown Symbol") : symbol.trimmed());
}

struct RecordedIdentity {
    QString exchange{};
    QString market{};
    int bookTickerCount{0};
    int candleCount{0};
    qint64 startedAtNs{0};
};

RecordedIdentity readRecordedIdentity(const QString& sessionPath) {
    RecordedIdentity out{};
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return out;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return out;
    const auto manifest = doc.object();
    const auto identity = manifest.value(QStringLiteral("identity")).toObject();
    out.exchange = identity.value(QStringLiteral("exchange")).toString();
    out.market = identity.value(QStringLiteral("market")).toString();
    const auto capture = manifest.value(QStringLiteral("capture")).toObject();
    out.startedAtNs = capture.value(QStringLiteral("started_at_ns")).toInteger();
    const auto channels = manifest.value(QStringLiteral("channels")).toObject();
    const auto bookTicker = channels.value(QStringLiteral("bookticker")).toObject();
    out.bookTickerCount = bookTicker.value(QStringLiteral("declared_event_count")).toInt();
    const auto candles = channels.value(QStringLiteral("candles")).toObject();
    const auto candles2 = channels.value(QStringLiteral("candles2")).toObject();
    const int candlesCount = candles.value(QStringLiteral("declared_event_count")).toInt();
    const int candles2Count = candles2.value(QStringLiteral("declared_event_count")).toInt();
    out.candleCount = candles2Count > 0 ? candles2Count : candlesCount;
    return out;
}

struct BacktestResultSummary {
    bool valid{false};
    bool selectable{false};
    QString type{};
    QString pnlText{};
    QString rightText{};
};

BacktestResultSummary readBacktestResultSummary(const QString& manifestPath) {
    BacktestResultSummary summary{};
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) return summary;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return summary;

    const QJsonObject root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();
    summary.type = type;
    summary.valid = type == QStringLiteral("run.result.v2") || type == QStringLiteral("sweep.result.v1");
    summary.selectable = type == QStringLiteral("run.result.v2");
    if (type == QStringLiteral("sweep.result.v1")) {
        const qint64 points = root.value(QStringLiteral("points_evaluated")).toInteger();
        summary.rightText = points > 0 ? QStringLiteral("sweep %1 pts").arg(points) : QStringLiteral("sweep");
    }
    if (!summary.valid) return summary;

    const QJsonObject summaryObject = root.value(QStringLiteral("summary")).toObject();
    const qint64 initial = summaryObject.value(QStringLiteral("initial_balance_e8")).toInteger();
    const qint64 pnl = summaryObject.value(QStringLiteral("total_pnl_e8")).toInteger();
    if (initial <= 0) return summary;

    const qint64 bps = (pnl * 10000) / initial;
    const QString sign = bps > 0 ? QStringLiteral("+") : (bps < 0 ? QStringLiteral("-") : QString{});
    const qint64 absBps = bps < 0 ? -bps : bps;
    summary.pnlText = QStringLiteral("%1%2.%3%")
                          .arg(sign,
                               QString::number(absBps / 100),
                               QString::number(absBps % 100).rightJustified(2, QLatin1Char('0')));
    const qint64 maxDrawdown = summaryObject.value(QStringLiteral("max_drawdown_e8")).toInteger();
    const qint64 ddBps = maxDrawdown > 0 ? (maxDrawdown * 10000) / initial : 0;
    const qint64 fills = summaryObject.value(QStringLiteral("fills")).toInteger();
    summary.rightText = QStringLiteral("%1 | DD %2.%3% | F %4")
                            .arg(summary.pnlText,
                                 QString::number(ddBps / 100),
                                 QString::number(ddBps % 100).rightJustified(2, QLatin1Char('0')),
                                 QString::number(fills));
    return summary;
}

void appendBacktestResultRowsForDir(const QString& sessionPath,
                                    const QString& prefix,
                                    const QDir& dir,
                                    QStringList& seen,
                                    QVariantList& rows) {
    if (!dir.exists()) return;
    const QFileInfoList resultDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& resultDir : resultDirs) {
        if (resultDir.fileName() == QStringLiteral("sweeps")) continue;
        const QDir result(resultDir.absoluteFilePath());
        const QString manifestPath = result.absoluteFilePath(QStringLiteral("manifest.json"));
        if (!QFileInfo::exists(manifestPath)) continue;
        const BacktestResultSummary summary = readBacktestResultSummary(manifestPath);
        if (!summary.valid) continue;

        const QString resultPath = QDir::cleanPath(resultDir.absoluteFilePath());
        if (seen.contains(resultPath)) continue;
        seen.push_back(resultPath);

        QVariantMap row;
        row.insert(QStringLiteral("sessionPath"), sessionPath);
        row.insert(QStringLiteral("path"), resultPath);
        row.insert(QStringLiteral("label"), prefix + QLatin1Char(' ') + resultDir.fileName());
        row.insert(QStringLiteral("pnlText"), summary.pnlText);
        row.insert(QStringLiteral("rightText"), summary.rightText.isEmpty() ? summary.pnlText : summary.rightText);
        row.insert(QStringLiteral("type"), summary.type);
        row.insert(QStringLiteral("selectable"), summary.selectable);
        rows.push_back(row);
    }
}

void appendBacktestResultRowsForSession(const QString& sessionPath,
                                        const QString& prefix,
                                        QStringList& seen,
                                        QVariantList& rows) {
    if (sessionPath.trimmed().isEmpty()) return;
    const QDir backtestsDir(QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests")));
    appendBacktestResultRowsForDir(sessionPath, prefix, backtestsDir, seen, rows);
    appendBacktestResultRowsForDir(sessionPath, prefix, QDir(backtestsDir.absoluteFilePath(QStringLiteral("sweeps"))), seen, rows);
}

}  // namespace

ViewerSourceListModel::ViewerSourceListModel(QObject* parent)
    : QAbstractListModel(parent) {
    rebuildEntries_();
}

void ViewerSourceListModel::reload() {
    rebuildEntries_();
}

QString ViewerSourceListModel::sessionPath(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.sessionPath;
    }
    return {};
}

QString ViewerSourceListModel::sourceKind(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.sourceKind;
    }
    return {};
}

QString ViewerSourceListModel::exchange(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.exchange;
    }
    return {};
}

QString ViewerSourceListModel::market(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.market;
    }
    return {};
}

QString ViewerSourceListModel::label(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.label;
    }
    return {};
}

QString ViewerSourceListModel::sourceIdAt(int index) const {
    if (index < 0 || index >= entries_.size()) return {};
    return entries_.at(index).id;
}

QString ViewerSourceListModel::labelAt(int index) const {
    if (index < 0 || index >= entries_.size()) return {};
    return entries_.at(index).label;
}

QString ViewerSourceListModel::groupAt(int index) const {
    if (index < 0 || index >= entries_.size()) return {};
    return entries_.at(index).group;
}

bool ViewerSourceListModel::hasSource(const QString& sourceId) const {
    return indexOfSource(sourceId) >= 0;
}

int ViewerSourceListModel::indexOfSource(const QString& sourceId) const {
    for (qsizetype i = 0; i < entries_.size(); ++i) {
        if (entries_.at(i).id == sourceId) return static_cast<int>(i);
    }
    return -1;
}

int ViewerSourceListModel::bookTickerCount(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.bookTickerCount;
    }
    return 0;
}

QString ViewerSourceListModel::sourceSummary(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.sourceSummary;
    }
    return {};
}

int ViewerSourceListModel::backtestCount(const QString& sourceId) const {
    for (const auto& entry : entries_) {
        if (entry.id == sourceId) return entry.backtestCount;
    }
    return 0;
}

QVariantList ViewerSourceListModel::backtestResultRows(const QString& primarySourceId, const QString& secondarySourceId) const {
    QVariantList rows;
    QStringList seen;
    appendBacktestResultRowsForSession(sessionPath(primarySourceId), QStringLiteral("A"), seen, rows);
    appendBacktestResultRowsForSession(sessionPath(secondarySourceId), QStringLiteral("B"), seen, rows);
    return rows;
}

QString ViewerSourceListModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

QObject* ViewerSourceListModel::captureViewModel() const {
    return captureVm_;
}

void ViewerSourceListModel::setCaptureViewModel(QObject* captureViewModel) {
    auto* typedVm = qobject_cast<CaptureViewModel*>(captureViewModel);
    if (typedVm == captureVm_) return;
    captureVm_ = typedVm;
    reconnectCaptureVm_();
    rebuildEntries_();
    emit captureViewModelChanged();
}

int ViewerSourceListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant ViewerSourceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size()) return {};

    const auto& entry = entries_.at(index.row());
    switch (role) {
        case IdRole: return entry.id;
        case LabelRole: return entry.label;
        case GroupRole: return entry.group;
        case GroupTitleRole: return entry.groupTitle;
        case SourceKindRole: return entry.sourceKind;
        case SessionPathRole: return entry.sessionPath;
        case SymbolRole: return entry.symbol;
        case ExchangeRole: return entry.exchange;
        case MarketRole: return entry.market;
        case LiveAvailableRole: return entry.liveAvailable;
        case BookTickerCountRole: return entry.bookTickerCount;
        case SourceSummaryRole: return entry.sourceSummary;
        case BacktestCountRole: return entry.backtestCount;
        default: return {};
    }
}

QHash<int, QByteArray> ViewerSourceListModel::roleNames() const {
    return {
        {IdRole, "id"},
        {LabelRole, "label"},
        {GroupRole, "group"},
        {GroupTitleRole, "groupTitle"},
        {SourceKindRole, "sourceKind"},
        {SessionPathRole, "sessionPath"},
        {SymbolRole, "symbol"},
        {ExchangeRole, "exchange"},
        {MarketRole, "market"},
        {LiveAvailableRole, "liveAvailable"},
        {BookTickerCountRole, "bookTickerCount"},
        {SourceSummaryRole, "sourceSummary"},
        {BacktestCountRole, "backtestCount"},
    };
}

void ViewerSourceListModel::reconnectCaptureVm_() {
    if (captureSourcesConnection_) disconnect(captureSourcesConnection_);
    if (captureVm_ == nullptr) return;
    captureSourcesConnection_ =
        connect(captureVm_, &CaptureViewModel::activeLiveSourcesChanged, this, &ViewerSourceListModel::rebuildEntries_);
}

QVariantList ViewerSourceListModel::currentLiveSources_() const {
    return captureVm_ != nullptr ? captureVm_->activeLiveSources() : QVariantList{};
}

void ViewerSourceListModel::rebuildEntries_() {
    QList<Entry> nextEntries;

    const auto liveSources = currentLiveSources_();
    nextEntries.reserve(static_cast<qsizetype>(liveSources.size()));
    for (const auto& sourceValue : liveSources) {
        const QVariantMap source = sourceValue.toMap();
        Entry entry{};
        entry.id = source.value(QStringLiteral("id")).toString();
        entry.exchange = source.value(QStringLiteral("exchange")).toString();
        entry.market = source.value(QStringLiteral("market")).toString();
        entry.symbol = source.value(QStringLiteral("symbol")).toString();
        entry.sourceKind = QStringLiteral("live");
        entry.group = QStringLiteral("live");
        entry.groupTitle = QStringLiteral("Live");
        entry.sessionPath = source.value(QStringLiteral("sessionPath")).toString();
        entry.liveAvailable = source.value(QStringLiteral("liveAvailable"), true).toBool();
        entry.bookTickerCount = source.value(QStringLiteral("bookTickerCount"), 0).toInt();
        entry.sourceSummary = sessionBacktestSummaryText(entry.bookTickerCount, {}, source.value(QStringLiteral("startedAtNs"), 0).toLongLong());
        entry.label = source.value(QStringLiteral("label")).toString();
        if (entry.label.isEmpty()) entry.label = buildLiveLabel(entry.exchange, entry.market, entry.symbol);
        if (!entry.id.isEmpty()) nextEntries.push_back(std::move(entry));
    }

    QDir recordingsDir(recordingsRoot());
    if (recordingsDir.exists()) {
        const auto recordedEntries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                             QDir::Time | QDir::Reversed);
        for (const auto& recordedId : recordedEntries) {
            Entry entry{};
            entry.id = QStringLiteral("recorded:%1").arg(recordedId);
            entry.label = recordedId;
            entry.group = QStringLiteral("recorded");
            entry.groupTitle = QStringLiteral("Recorded");
            entry.sourceKind = QStringLiteral("recorded");
            entry.sessionPath = recordingsDir.absoluteFilePath(recordedId);
            const BacktestLegCounts backtestCounts = backtestLegCountsForSession(recordingsRoot(), recordedId);
            entry.backtestCount = backtestCounts.firstLeg;
            const auto identity = readRecordedIdentity(entry.sessionPath);
            entry.exchange = identity.exchange;
            entry.market = identity.market;
            entry.bookTickerCount = identity.bookTickerCount;
            entry.sourceSummary = sessionBacktestSummaryText(entry.bookTickerCount, backtestCounts, identity.startedAtNs);
            if (identity.candleCount > 0) {
                entry.sourceSummary += QStringLiteral(" | C %1").arg(identity.candleCount);
            }
            nextEntries.push_back(std::move(entry));
        }
    }

    beginResetModel();
    entries_ = std::move(nextEntries);
    endResetModel();
}

}  // namespace hftrec::gui

