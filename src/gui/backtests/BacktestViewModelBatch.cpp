#include "gui/backtests/BacktestViewModel.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaObject>
#include <QSet>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <exception>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hft_backtest/backtest.hpp"
#include "hft_backtest/backtest_sweep.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "gui/backtests/BacktestBatchAnalysisHelpers.hpp"
#include "gui/backtests/BacktestBatchSweepHelpers.hpp"
#include "gui/backtests/BacktestExecutionConfigHelpers.hpp"
#include "gui/backtests/BacktestSessionHelpers.hpp"
#include "gui/backtests/BacktestStrategyConfigHelpers.hpp"

namespace hftrec::gui {
namespace {

struct BatchProgressContext {
    BacktestViewModel* vm{nullptr};
    int pairIndex{0};
    int pairCount{1};
    QString label{};
};

struct PreparedBatchPair {
    BatchSweepPair pair{};
    QString configPath{};
    QString outputPath{};
    hft_backtest::BacktestSweepRequest request{};
};

struct PreparedBasisChainPair {
    BatchSweepPair pair{};
    QString configPath{};
    QString outputPath{};
    hft_backtest::BacktestRunRequest request{};
};

bool batchProgressCallback(const hft_backtest::BacktestProgress& progress, void* userData) noexcept {
    auto* ctx = static_cast<BatchProgressContext*>(userData);
    if (ctx == nullptr || ctx->vm == nullptr) return false;
    const int pairCount = std::max(1, ctx->pairCount);
    const int percent = std::clamp(((ctx->pairIndex * 100) + static_cast<int>(progress.percent)) / pairCount, 0, 99);
    const QString text = QStringLiteral("%1: %2/%3 points")
        .arg(ctx->label)
        .arg(static_cast<qulonglong>(progress.eventsDone))
        .arg(static_cast<qulonglong>(progress.eventsTotal));
    QMetaObject::invokeMethod(ctx->vm, [vm = ctx->vm, percent, text] {
        vm->applyWorkerProgress(percent, text);
    }, Qt::QueuedConnection);
    return !ctx->vm->workerCancelRequested();
}

bool basisChainProgressCallback(const hft_backtest::BacktestProgress& progress, void* userData) noexcept {
    auto* ctx = static_cast<BatchProgressContext*>(userData);
    if (ctx == nullptr || ctx->vm == nullptr) return false;
    const int pairCount = std::max(1, ctx->pairCount);
    const int percent = std::clamp(((ctx->pairIndex * 100) + static_cast<int>(progress.percent)) / pairCount, 0, 99);
    const QString text = QStringLiteral("%1: %2/%3 events")
        .arg(ctx->label)
        .arg(static_cast<qulonglong>(progress.eventsDone))
        .arg(static_cast<qulonglong>(progress.eventsTotal));
    QMetaObject::invokeMethod(ctx->vm, [vm = ctx->vm, percent, text] {
        vm->applyWorkerProgress(percent, text);
    }, Qt::QueuedConnection);
    return !ctx->vm->workerCancelRequested();
}

bool writeJson(const QString& path, const QJsonObject& object) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.error() == QFileDevice::NoError;
}

QVector<QVariantMap> readJsonlMaps(const QString& path) {
    QVector<QVariantMap> out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;
        out.push_back(doc.object().toVariantMap());
    }
    return out;
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

bool appendJsonl(QFile& file, const QVariantMap& map) {
    file.write(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
    file.write("\n");
    return file.error() == QFileDevice::NoError;
}

QString normalizedExchange(const QString& value) {
    return value.trimmed().toLower();
}

QString firstJsonString(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString();
    if (value.isArray() && !value.toArray().isEmpty()) return value.toArray().at(0).toString();
    return {};
}

QString manifestString(const QJsonObject& manifest, const QString& key) {
    const QString direct = firstJsonString(manifest, key);
    if (!direct.isEmpty()) return direct;
    return firstJsonString(manifest.value(QStringLiteral("identity")).toObject(), key);
}

qint64 channelRows(const QJsonObject& manifest, const QString& channel) {
    return manifest.value(QStringLiteral("channels"))
        .toObject()
        .value(channel)
        .toObject()
        .value(QStringLiteral("declared_event_count"))
        .toInteger();
}

qint64 sessionCandleRows(const QJsonObject& manifest) {
    const qint64 candles2 = channelRows(manifest, QStringLiteral("candles2"));
    if (candles2 > 0) return candles2;
    const qint64 candlesV2 = channelRows(manifest, QStringLiteral("candlesv2"));
    if (candlesV2 > 0) return candlesV2;
    return channelRows(manifest, QStringLiteral("candles"));
}

qint64 metadataInt64(const QString& sessionPath, const QString& key) {
    const QJsonObject metadata = readJsonObject(QDir(sessionPath).absoluteFilePath(QStringLiteral("instrument_metadata.json")));
    return metadata.value(key).toInteger();
}

BatchSweepSessionInfo sessionInfoForPath(const QString& path) {
    BatchSweepSessionInfo out;
    out.path = path;
    out.sessionId = QFileInfo(path).fileName();
    out.sessionDirExists = QFileInfo(path).isDir();
    const QString manifestPath = QDir(path).absoluteFilePath(QStringLiteral("manifest.json"));
    out.manifestPresent = QFileInfo(manifestPath).isFile();
    const QJsonObject manifest = readJsonObject(manifestPath);
    out.exchange = normalizedExchange(manifestString(manifest, QStringLiteral("exchange")));
    out.market = manifestString(manifest, QStringLiteral("market")).trimmed().toLower();
    out.symbol = manifestString(manifest, QStringLiteral("symbols")).trimmed().toUpper();
    if (out.symbol.isEmpty()) out.symbol = symbolFromSessionId(out.sessionId).toUpper();
    out.canonicalSymbol = batchCanonicalSymbol(out.symbol);
    out.venue = venueSectionFor(out.exchange, out.market);
    out.candleRows = sessionCandleRows(manifest);
    out.expiryUtcNs = metadataInt64(path, QStringLiteral("expiry_utc_ns"));
    out.priceBasisQtyE8 = metadataInt64(path, QStringLiteral("price_basis_qty_e8"));
    return out;
}

bool basisSpotMarket(const QString& market) {
    const QString m = market.trimmed().toLower();
    return m.contains(QStringLiteral("spot")) || m.contains(QStringLiteral("share")) ||
           m.contains(QStringLiteral("stock")) || m.contains(QStringLiteral("misx"));
}

qint64 basisExpiryNs(const QString& sessionPath) {
    return metadataInt64(sessionPath, QStringLiteral("expiry_utc_ns"));
}

QString basisChainPairSlug(const BatchSweepPair& pair) {
    const qint64 expiry = pair.second.expiryUtcNs > 0 ? pair.second.expiryUtcNs : basisExpiryNs(pair.second.path);
    const QString expiryPart = expiry > 0 ? QString::number(expiry) : pair.second.sessionId;
    return cleanRunSlugPart(pair.first.symbol) + QStringLiteral("-spot-") +
           cleanRunSlugPart(pair.second.symbol) + QStringLiteral("-") +
           cleanRunSlugPart(expiryPart) + QStringLiteral("-p") +
           QString::number(pair.pairId);
}

QSet<QString> cleanedPathSet(const QVariantList& values) {
    QSet<QString> out;
    for (const QVariant& value : values) {
        const QString path = QDir::cleanPath(value.toString());
        if (!path.trimmed().isEmpty()) out.insert(path);
    }
    return out;
}

qint64 timeframeSeconds(QString timeframe) {
    timeframe = timeframe.trimmed();
    if (timeframe.isEmpty()) return 60;
    const QChar unit = timeframe.at(timeframe.size() - 1);
    bool ok = false;
    const qint64 value = timeframe.left(timeframe.size() - 1).toLongLong(&ok);
    if (!ok || value <= 0) return 60;
    if (unit == QLatin1Char('s') || unit == QLatin1Char('S')) return value;
    if (unit == QLatin1Char('m')) return value * 60;
    if (unit == QLatin1Char('h') || unit == QLatin1Char('H')) return value * 60 * 60;
    if (unit == QLatin1Char('d') || unit == QLatin1Char('D')) return value * 24 * 60 * 60;
    return 60;
}

QHash<QString, QString> basisChainStrategyOverrides(const QString& groupPath) {
    const QJsonObject manifest = readJsonObject(QDir(groupPath).absoluteFilePath(QStringLiteral("basis_chain_manifest.json")));
    QHash<QString, QString> out;
    out.insert(QStringLiteral("source"), QStringLiteral("candles"));
    out.insert(QStringLiteral("timeframe_sec"), QString::number(timeframeSeconds(manifest.value(QStringLiteral("timeframe")).toString())));
    return out;
}

QVector<BatchSweepPair> buildBasisChainPairs(const QString& groupPath,
                                             const QSet<QString>& enabledFuturePaths,
                                             QVariantList* skippedRows = nullptr) {
    QVector<BatchSweepSessionInfo> sessions;
    const QString requested = QDir::cleanPath(groupPath);
    const auto discovery = hftrec::recordings::discoverRecordings(hftrec::recordings::defaultRecordingsRoot());
    for (const auto& group : discovery.groups) {
        if (QDir::cleanPath(QString::fromStdString(group.path.string())) != requested) continue;
        sessions.reserve(static_cast<int>(group.sessions.size()));
        for (const auto& session : group.sessions) {
            sessions.push_back(sessionInfoForPath(QString::fromStdString(session.path.string())));
        }
        break;
    }
    if (sessions.empty()) {
        QVariantMap row;
        row.insert(QStringLiteral("reason"), QStringLiteral("basis group not found"));
        row.insert(QStringLiteral("groupPath"), requested);
        if (skippedRows != nullptr) skippedRows->push_back(row);
        return {};
    }

    auto appendSkippedSession = [&](const BatchSweepSessionInfo& session, const QString& reason) {
        if (skippedRows == nullptr || reason.isEmpty()) return;
        QVariantMap row;
        row.insert(QStringLiteral("reason"), reason);
        row.insert(QStringLiteral("sessionId"), session.sessionId);
        row.insert(QStringLiteral("symbol"), session.symbol);
        row.insert(QStringLiteral("exchange"), session.exchange);
        row.insert(QStringLiteral("market"), session.market);
        row.insert(QStringLiteral("sessionPath"), session.path);
        row.insert(QStringLiteral("candleRows"), session.candleRows);
        row.insert(QStringLiteral("expiryUtcNs"), session.expiryUtcNs);
        row.insert(QStringLiteral("priceBasisQtyE8"), session.priceBasisQtyE8);
        skippedRows->push_back(row);
    };

    BatchSweepSessionInfo spot;
    QVector<BatchSweepSessionInfo> futures;
    for (const BatchSweepSessionInfo& session : sessions) {
        if (basisSpotMarket(session.market)) {
            const QString reason = basisChainSpotSkipReason(session);
            if (!reason.isEmpty()) {
                appendSkippedSession(session, reason);
                continue;
            }
            if (!spot.path.isEmpty()) {
                appendSkippedSession(session, QStringLiteral("extra spot leg ignored"));
                continue;
            }
            spot = session;
            continue;
        }
        if (isBatchFuturesMarket(session.market)) {
            if (!enabledFuturePaths.empty() && !enabledFuturePaths.contains(QDir::cleanPath(session.path))) {
                appendSkippedSession(session, QStringLiteral("future disabled in MOEX Basis"));
                continue;
            }
            const QString reason = basisChainFutureSkipReason(session);
            if (!reason.isEmpty()) {
                appendSkippedSession(session, reason);
                continue;
            }
            futures.push_back(session);
        }
    }
    if (spot.path.isEmpty()) {
        QVariantMap row;
        row.insert(QStringLiteral("reason"), QStringLiteral("basis group has no spot leg"));
        row.insert(QStringLiteral("groupPath"), requested);
        if (skippedRows != nullptr) skippedRows->push_back(row);
        return {};
    }
    std::sort(futures.begin(), futures.end(), [](const BatchSweepSessionInfo& lhs, const BatchSweepSessionInfo& rhs) {
        const qint64 le = lhs.expiryUtcNs;
        const qint64 re = rhs.expiryUtcNs;
        if (le != re) return (le == 0 ? std::numeric_limits<qint64>::max() : le) <
                             (re == 0 ? std::numeric_limits<qint64>::max() : re);
        return lhs.symbol < rhs.symbol;
    });

    QVector<BatchSweepPair> pairs;
    pairs.reserve(futures.size());
    QHash<qint64, int> expiryCounts;
    for (const BatchSweepSessionInfo& future : futures) {
        const qint64 expiry = future.expiryUtcNs;
        if (expiry > 0) expiryCounts[expiry] += 1;
    }
    int pairId = 0;
    for (const BatchSweepSessionInfo& future : futures) {
        const qint64 expiry = future.expiryUtcNs;
        if (expiry > 0 && expiryCounts.value(expiry) > 1) {
            QVariantMap row;
            row.insert(QStringLiteral("reason"), QStringLiteral("duplicate expiry; uncheck conflicting futures in MOEX Basis"));
            row.insert(QStringLiteral("symbol"), future.symbol);
            row.insert(QStringLiteral("expiryUtcNs"), expiry);
            row.insert(QStringLiteral("sessionPath"), future.path);
            if (skippedRows != nullptr) skippedRows->push_back(row);
            continue;
        }
        if (!isVenueSectionKnown(spot.exchange, spot.market) || !isVenueSectionKnown(future.exchange, future.market)) {
            QVariantMap row;
            row.insert(QStringLiteral("reason"), QStringLiteral("unsupported venue"));
            row.insert(QStringLiteral("symbol"), future.symbol);
            row.insert(QStringLiteral("exchangePair"), QStringLiteral("%1/%2").arg(spot.exchange, future.exchange));
            if (skippedRows != nullptr) skippedRows->push_back(row);
            continue;
        }
        BatchSweepPair pair;
        pair.pairId = ++pairId;
        pair.first = spot;
        pair.second = future;
        pairs.push_back(std::move(pair));
    }
    if (pairs.empty()) {
        QVariantMap row;
        row.insert(QStringLiteral("reason"), QStringLiteral("basis group has no runnable futures legs"));
        row.insert(QStringLiteral("groupPath"), requested);
        if (skippedRows != nullptr) skippedRows->push_back(row);
    }
    return pairs;
}

QString cleanPairSlug(const BatchSweepPair& pair) {
    const QString symbol = pair.first.canonicalSymbol.isEmpty() ? pair.first.symbol : pair.first.canonicalSymbol;
    return cleanRunSlugPart(symbol) + QStringLiteral("-") +
           cleanRunSlugPart(pair.first.exchange) + QStringLiteral("-") +
           cleanRunSlugPart(pair.second.exchange) + QStringLiteral("-p") +
           QString::number(pair.pairId);
}

QVariantMap enrichedBatchRow(const QVariantMap& row,
                             const PreparedBatchPair& pair,
                             const hft_backtest::BacktestSweepResult& result,
                             const QString& batchId) {
    QVariantMap out = row;
    const QString exchangePair = batchExchangePairLabel(pair.pair);
    const QString symbol = pair.pair.first.canonicalSymbol.isEmpty() ? pair.pair.first.symbol : pair.pair.first.canonicalSymbol;
    out.insert(QStringLiteral("batchId"), batchId);
    out.insert(QStringLiteral("sweepId"), QString::fromStdString(result.sweepId));
    out.insert(QStringLiteral("pairId"), pair.pair.pairId);
    out.insert(QStringLiteral("symbol"), symbol);
    out.insert(QStringLiteral("exchangePair"), exchangePair);
    out.insert(QStringLiteral("exchange_pair"), exchangePair);
    out.insert(QStringLiteral("firstExchange"), pair.pair.first.exchange);
    out.insert(QStringLiteral("secondExchange"), pair.pair.second.exchange);
    out.insert(QStringLiteral("firstMarket"), pair.pair.first.market);
    out.insert(QStringLiteral("secondMarket"), pair.pair.second.market);
    out.insert(QStringLiteral("firstSymbol"), pair.pair.first.symbol);
    out.insert(QStringLiteral("secondSymbol"), pair.pair.second.symbol);
    out.insert(QStringLiteral("firstSessionPath"), pair.pair.first.path);
    out.insert(QStringLiteral("secondSessionPath"), pair.pair.second.path);
    out.insert(QStringLiteral("sweepPath"), QString::fromStdString(result.resultPath.generic_string()));
    out.insert(QStringLiteral("totalPnlE8"), out.value(QStringLiteral("total_pnl_e8")).toLongLong());
    out.insert(QStringLiteral("maxDrawdownE8"), out.value(QStringLiteral("max_drawdown_e8")).toLongLong());
    out.insert(QStringLiteral("riskStopped"), out.value(QStringLiteral("risk_stopped")).toBool());
    out.insert(QStringLiteral("liquidated"), out.value(QStringLiteral("liquidated")).toBool());
    out.insert(QStringLiteral("paramsLabel"), batchParamsLabel(out.value(QStringLiteral("params")).toMap()));
    return out;
}

QString basisChainNoSignalReason(const hft_backtest::BacktestRunResult& result) {
    for (const std::string& warning : result.warnings) {
        const QString text = QString::fromStdString(warning).trimmed();
        if (text.contains(QStringLiteral("candle"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("metadata"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("price_basis"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("expiry"), Qt::CaseInsensitive) ||
            text.contains(QStringLiteral("session"), Qt::CaseInsensitive)) {
            return text;
        }
    }
    if (result.events == 0) return QStringLiteral("basis strategy produced no strategy_spread rows: no replay events");
    return QStringLiteral("basis strategy produced no strategy_spread rows");
}

QVariantMap basisChainBatchRow(const PreparedBasisChainPair& pair,
                               const hft_backtest::BacktestRunResult& result,
                               const QString& batchId) {
    const std::string_view statusView = hft_backtest::statusToString(result.status);
    const QString backtestStatus = QString::fromUtf8(statusView.data(), static_cast<qsizetype>(statusView.size()));
    QString status = backtestStatus;
    QString error = QString::fromStdString(result.error);
    if (hft_backtest::isOk(result.status) && result.strategySpreadRows == 0) {
        status = QStringLiteral("NoSignal");
        if (error.trimmed().isEmpty()) error = basisChainNoSignalReason(result);
    }
    const QString exchangePair = QStringLiteral("%1/%2").arg(pair.pair.first.exchange, pair.pair.second.exchange);
    QVariantMap out;
    out.insert(QStringLiteral("batchId"), batchId);
    out.insert(QStringLiteral("runId"), QString::fromStdString(result.runId));
    out.insert(QStringLiteral("pairId"), pair.pair.pairId);
    out.insert(QStringLiteral("status"), status);
    out.insert(QStringLiteral("backtestStatus"), backtestStatus);
    out.insert(QStringLiteral("error"), error);
    out.insert(QStringLiteral("symbol"), pair.pair.second.symbol);
    out.insert(QStringLiteral("exchangePair"), exchangePair);
    out.insert(QStringLiteral("exchange_pair"), exchangePair);
    out.insert(QStringLiteral("firstExchange"), pair.pair.first.exchange);
    out.insert(QStringLiteral("secondExchange"), pair.pair.second.exchange);
    out.insert(QStringLiteral("firstMarket"), pair.pair.first.market);
    out.insert(QStringLiteral("secondMarket"), pair.pair.second.market);
    out.insert(QStringLiteral("firstSymbol"), pair.pair.first.symbol);
    out.insert(QStringLiteral("secondSymbol"), pair.pair.second.symbol);
    out.insert(QStringLiteral("firstSessionPath"), pair.pair.first.path);
    out.insert(QStringLiteral("secondSessionPath"), pair.pair.second.path);
    out.insert(QStringLiteral("resultPath"), QString::fromStdString(result.resultPath.generic_string()));
    out.insert(QStringLiteral("totalPnlE8"), static_cast<qint64>(result.totalPnlE8));
    out.insert(QStringLiteral("total_pnl_e8"), static_cast<qint64>(result.totalPnlE8));
    out.insert(QStringLiteral("maxDrawdownE8"), static_cast<qint64>(result.maxDrawdownE8));
    out.insert(QStringLiteral("max_drawdown_e8"), static_cast<qint64>(result.maxDrawdownE8));
    out.insert(QStringLiteral("fills"), static_cast<qint64>(result.fills));
    out.insert(QStringLiteral("orders"), static_cast<qint64>(result.orders));
    out.insert(QStringLiteral("strategySpreadRows"), static_cast<qint64>(result.strategySpreadRows));
    out.insert(QStringLiteral("strategy_spread_rows"), static_cast<qint64>(result.strategySpreadRows));
    out.insert(QStringLiteral("riskStopped"), result.riskStopTsNs > 0);
    out.insert(QStringLiteral("risk_stopped"), result.riskStopTsNs > 0);
    out.insert(QStringLiteral("liquidated"), result.liquidationTsNs > 0);
    out.insert(QStringLiteral("params"), QVariantMap{});
    out.insert(QStringLiteral("paramsLabel"), QStringLiteral("fixed"));
    return out;
}

void trimTable(QVariantList& rows, qsizetype limit) {
    while (rows.size() > limit) rows.removeLast();
}

QVariantList choicesFromSessions(const QVariantList& sessions) {
    QVariantList out;
    for (const QVariant& value : sessions) {
        const QVariantMap row = value.toMap();
        if (!row.value(QStringLiteral("isGroup")).toBool() && !row.value(QStringLiteral("selectable"), true).toBool()) continue;
        QVariantMap choice;
        choice.insert(QStringLiteral("id"), row.value(QStringLiteral("id")).toString());
        choice.insert(QStringLiteral("label"), row.value(QStringLiteral("label")).toString());
        choice.insert(QStringLiteral("rightText"), row.value(QStringLiteral("rightText")).toString());
        choice.insert(QStringLiteral("isGroup"), row.value(QStringLiteral("isGroup")).toBool());
        out.push_back(choice);
    }
    return out;
}

}  // namespace

QVariantList BacktestViewModel::batchUniverseChoices() const {
    return choicesFromSessions(sessions_);
}

QString BacktestViewModel::batchUniverseId() const {
    if (!batchUniverseId_.trimmed().isEmpty() && !sessionRowById(sessions_, batchUniverseId_).isEmpty()) return batchUniverseId_;
    const QVariantMap selected = sessionRowById(sessions_, selectedSessionId_);
    const QString parentGroup = selected.value(QStringLiteral("parentGroupId")).toString().trimmed();
    if (!parentGroup.isEmpty()) return QStringLiteral("group:%1").arg(parentGroup);
    return selectedSessionId_;
}

void BacktestViewModel::setBatchUniverseId(const QString& value) {
    const QString next = value.trimmed();
    if (batchUniverseId_ == next) return;
    batchUniverseId_ = next;
    savePersistentConfig_();
    emit batchConfigChanged();
}

void BacktestViewModel::setBatchPairBudget(const QString& value) {
    const QString next = value.trimmed();
    if (batchPairBudget_ == next) return;
    batchPairBudget_ = next;
    savePersistentConfig_();
    emit batchConfigChanged();
}

void BacktestViewModel::setBatchOnlyFutures(bool value) {
    if (batchOnlyFutures_ == value) return;
    batchOnlyFutures_ = value;
    savePersistentConfig_();
    emit batchConfigChanged();
}

void BacktestViewModel::setBatchRawTableMode(const QString& value) {
    const QString next = value.trimmed().isEmpty() ? QStringLiteral("stable") : value.trimmed();
    if (batchRawTableMode_ == next) return;
    batchRawTableMode_ = next;
    savePersistentConfig_();
    emit batchConfigChanged();
}

QStringList BacktestViewModel::batchUniverseSessionPaths_() const {
    const QString id = batchUniverseId();
    const QVariantMap row = sessionRowById(sessions_, id);
    if (row.value(QStringLiteral("isGroup")).toBool()) return sessionPathsFromRow(row);
    const QString parentGroup = row.value(QStringLiteral("parentGroupId")).toString().trimmed();
    if (!parentGroup.isEmpty()) {
        const QVariantMap groupRow = sessionRowById(sessions_, QStringLiteral("group:%1").arg(parentGroup));
        const QStringList groupPaths = sessionPathsFromRow(groupRow);
        if (!groupPaths.empty()) return groupPaths;
    }
    const QStringList rowPaths = sessionPathsFromRow(row);
    if (!rowPaths.empty()) return rowPaths;
    return selectedSessionPaths_();
}

void BacktestViewModel::startBasisChainBatchBacktest(const QString& groupPath) {
    startBasisChainBatchBacktestForFutures(groupPath, {});
}

void BacktestViewModel::startBasisChainBatchBacktestForFutures(const QString& groupPath, const QVariantList& enabledFutureSessionPaths) {
    if (running_) return;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr || !strategyMetadataSupportsSessionCount(*metadata, 2)) {
        setStatusText_(QStringLiteral("Selected strategy does not support spot+future basis batch"));
        return;
    }

    QVariantList skipped;
    const QVector<BatchSweepPair> pairs = buildBasisChainPairs(groupPath, cleanedPathSet(enabledFutureSessionPaths), &skipped);
    if (pairs.empty()) {
        batchSkippedRows_ = skipped;
        batchStableRows_.clear();
        batchProfitRows_.clear();
        batchSymbolRows_.clear();
        batchPairRows_.clear();
        batchParamRows_.clear();
        batchSummaryCards_ = batchSummaryCardsFromRows(QVariantList{}, skipped, 0, 0);
        batchPairMatrixColumns_.clear();
        batchPairMatrixCells_.clear();
        batchTimeRows_.clear();
        batchPlateauRows_.clear();
        batchSummaryText_ = QStringLiteral("No runnable spot+future pairs in basis group");
        emit batchResultsChanged();
        setStatusText_(batchSummaryText_);
        return;
    }

    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);
    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting basis chain batch"));
    setStatusText_(QStringLiteral("Basis chain batch running"));

    const QString batchId = QStringLiteral("basis-chain-") + runId_();
    const QString groupRoot = QDir::cleanPath(groupPath);
    const QString batchRoot = QDir(groupRoot).absoluteFilePath(QStringLiteral("backtests/basis_chain_batches/%1").arg(batchId));
    const QHash<QString, QString> strategyOverrides = basisChainStrategyOverrides(groupRoot);
    QDir().mkpath(batchRoot);

    const quint64 pingLatency = latencyValue_(pingLatencyUs_, 1000);
    const quint64 latencySeed = latencyValue_(latencySeed_, 0);
    const quint64 marketDataLatency = latencyValue_(marketDataLatencyUs_, 250);
    const quint64 marketDataJitter = latencyValue_(marketDataJitterUs_, 100);
    const quint64 marketOrderLatency = latencyValue_(marketOrderLatencyUs_, pingLatency);
    const quint64 marketOrderJitter = latencyValue_(marketOrderJitterUs_, 0);
    const quint64 limitOrderLatency = latencyValue_(limitOrderLatencyUs_, pingLatency);
    const quint64 limitOrderJitter = latencyValue_(limitOrderJitterUs_, 0);
    const quint64 cancelOrderLatency = latencyValue_(cancelOrderLatencyUs_, limitOrderLatency);
    const quint64 cancelOrderJitter = latencyValue_(cancelOrderJitterUs_, limitOrderJitter);
    const quint64 userDataLatency = latencyValue_(userDataLatencyUs_, 0);
    const quint64 userDataJitter = latencyValue_(userDataJitterUs_, 0);
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const bool rateLimitsEnabled = rateLimitsEnabled_;
    const bool strictRateLimitsEnabled = rateLimitsEnabled && strictRateLimitsEnabled_;
    const QString strategy = selectedStrategy_;
    const QString indicatorProfile = selectedIndicatorProfile_;

    std::vector<PreparedBasisChainPair> prepared;
    prepared.reserve(static_cast<std::size_t>(pairs.size()));
    QString firstConfigError;
    for (const BatchSweepPair& pair : pairs) {
        const QStringList sessionPaths{pair.first.path, pair.second.path};
        const QString pairSlug = basisChainPairSlug(pair);
        const RunConfigWriteResult config = writeRunConfigForSessionPaths_(QStringLiteral("basis_chain_batches/%1/pair_configs/%2").arg(batchId, pairSlug),
                                                                           sessionPaths,
                                                                           strategyOverrides,
                                                                           true,
                                                                           false);
        if (!config.ok()) {
            if (firstConfigError.isEmpty()) firstConfigError = config.error;
            QVariantMap row;
            row.insert(QStringLiteral("reason"), QStringLiteral("failed to write basis pair config: %1").arg(config.error));
            row.insert(QStringLiteral("symbol"), pair.second.symbol);
            row.insert(QStringLiteral("exchangePair"), QStringLiteral("%1/%2").arg(pair.first.exchange, pair.second.exchange));
            skipped.push_back(row);
            continue;
        }

        PreparedBasisChainPair item;
        item.pair = pair;
        item.configPath = config.path;
        item.outputPath = QDir(batchRoot).absoluteFilePath(QStringLiteral("runs/%1").arg(pairSlug));
        item.request.sessionPath = pair.first.path.toStdString();
        hft_backtest::BacktestSessionRequest leg;
        leg.path = pair.second.path.toStdString();
        leg.venue = pair.second.venue.toStdString();
        leg.symbol = pair.second.symbol.toStdString();
        item.request.sessions.push_back(std::move(leg));
        item.request.configPath = item.configPath.toStdString();
        item.request.outputPath = item.outputPath.toStdString();
        item.request.strategy = strategy.toStdString();
        item.request.indicatorProfile = indicatorProfile.toStdString();
        item.request.runId = QStringLiteral("%1-%2").arg(batchId, pairSlug).toStdString();
        item.request.requestId = item.request.runId;
        item.request.latencySeed = latencySeed;
        item.request.marketDataLatency.baseUs = marketDataLatency;
        item.request.marketDataLatency.jitterUs = marketDataJitter;
        item.request.marketOrderLatency.baseUs = marketOrderLatency;
        item.request.marketOrderLatency.jitterUs = marketOrderJitter;
        item.request.limitOrderLatency.baseUs = limitOrderLatency;
        item.request.limitOrderLatency.jitterUs = limitOrderJitter;
        item.request.cancelOrderLatency.baseUs = cancelOrderLatency;
        item.request.cancelOrderLatency.jitterUs = cancelOrderJitter;
        item.request.userDataLatency.baseUs = userDataLatency;
        item.request.userDataLatency.jitterUs = userDataJitter;
        item.request.orderLatencyUs = marketOrderLatency;
        item.request.cancelLatencyUs = cancelOrderLatency;
        item.request.initialBalanceE8 = initialBalance;
        const std::vector<QVariantMap> venueRows = venueExecutionRowsForPaths_(sessionPaths);
        for (const QVariantMap& row : venueRows) {
            item.request.legInitialBalancesE8.push_back(decimalE8Value_(row.value(QStringLiteral("initialBalanceUsdt")).toString(), initialBalance));
            item.request.feeSchedules.push_back(feeScheduleFromVenueRow(row));
            hft_backtest::BacktestLatencySchedule latency;
            latency.exchange = row.value(QStringLiteral("exchange")).toString().toStdString();
            latency.market = row.value(QStringLiteral("market")).toString().toStdString();
            latency.marketData.baseUs = latencyValue_(row.value(QStringLiteral("marketDataLatencyUs")).toString(), marketDataLatency);
            latency.marketData.jitterUs = latencyValue_(row.value(QStringLiteral("marketDataJitterUs")).toString(), marketDataJitter);
            latency.marketOrder.baseUs = latencyValue_(row.value(QStringLiteral("marketOrderLatencyUs")).toString(), marketOrderLatency);
            latency.marketOrder.jitterUs = latencyValue_(row.value(QStringLiteral("marketOrderJitterUs")).toString(), marketOrderJitter);
            latency.limitOrder.baseUs = latencyValue_(row.value(QStringLiteral("limitOrderLatencyUs")).toString(), limitOrderLatency);
            latency.limitOrder.jitterUs = latencyValue_(row.value(QStringLiteral("limitOrderJitterUs")).toString(), limitOrderJitter);
            latency.cancelOrder.baseUs = latencyValue_(row.value(QStringLiteral("cancelOrderLatencyUs")).toString(), cancelOrderLatency);
            latency.cancelOrder.jitterUs = latencyValue_(row.value(QStringLiteral("cancelOrderJitterUs")).toString(), cancelOrderJitter);
            latency.userData.baseUs = latencyValue_(row.value(QStringLiteral("userDataLatencyUs")).toString(), userDataLatency);
            latency.userData.jitterUs = latencyValue_(row.value(QStringLiteral("userDataJitterUs")).toString(), userDataJitter);
            item.request.latencySchedules.push_back(std::move(latency));
            hft_backtest::BacktestRateLimitSchedule rateLimit = rateLimitScheduleFromVenueRow(row);
            if (!rateLimit.buckets.empty() || !rateLimit.actions.empty()) item.request.rateLimitSchedules.push_back(std::move(rateLimit));
        }
        item.request.rateLimitsEnabled = rateLimitsEnabled;
        item.request.strictRateLimitRejects = strictRateLimitsEnabled;
        item.request.executionPipeline = guiBacktestExecutionPipeline();
        item.request.writeArtifacts = true;
        item.request.captureStrategySpread = true;
        prepared.push_back(std::move(item));
    }

    if (prepared.empty()) {
        setRunning_(false);
        setProgress_(100, QStringLiteral("Basis chain batch failed"));
        batchSkippedRows_ = skipped;
        batchSummaryCards_ = batchSummaryCardsFromRows(QVariantList{}, skipped, 0, 0);
        batchPairMatrixColumns_.clear();
        batchPairMatrixCells_.clear();
        batchTimeRows_.clear();
        batchPlateauRows_.clear();
        batchSummaryText_ = firstConfigError.isEmpty()
            ? QStringLiteral("No runnable basis pairs after config generation")
            : QStringLiteral("No runnable basis pairs after config generation: %1").arg(firstConfigError);
        emit batchResultsChanged();
        setStatusText_(batchSummaryText_);
        return;
    }

    configureWorkerThreadStack_();
    worker_ = std::thread([this, prepared = std::move(prepared), skipped = std::move(skipped), batchId, batchRoot, strategy] {
        try {
            QVariantList batchRows;
            QVariantList skippedRows = skipped;
            QString errorText;
            const QString rowsPath = QDir(batchRoot).absoluteFilePath(QStringLiteral("batch_rows.jsonl"));
            QFile rowsFile(rowsPath);
            if (!rowsFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                errorText = QStringLiteral("failed to write basis chain batch rows");
            }

            for (int i = 0; errorText.isEmpty() && i < static_cast<int>(prepared.size()); ++i) {
                if (workerCancelRequested()) {
                    errorText = QStringLiteral("cancelled");
                    break;
                }
                const PreparedBasisChainPair& pair = prepared[static_cast<std::size_t>(i)];
                BatchProgressContext progressCtx{this, i, static_cast<int>(prepared.size()), pair.pair.second.symbol};
                const hft_backtest::BacktestRunResult result = hft_backtest::runBacktest(pair.request, basisChainProgressCallback, &progressCtx);
                QVariantMap row = basisChainBatchRow(pair, result, batchId);
                batchRows.push_back(row);
                if (!appendJsonl(rowsFile, row)) {
                    errorText = QStringLiteral("failed to append basis chain batch row");
                    break;
                }
            }
            rowsFile.close();

            QVariantList stableRows = batchStableRowsFromRows(batchRows);
            QVariantList profitRows = batchProfitRowsFromRows(batchRows);
            QVariantList symbolRows = batchSymbolRowsFromRows(batchRows);
            QVariantList pairRows = batchPairRowsFromRows(batchRows);
            QVariantList paramRows = batchParamRowsFromRows(batchRows);
            QVariantList summaryCards = batchSummaryCardsFromRows(batchRows, skippedRows, static_cast<int>(prepared.size()), static_cast<quint64>(prepared.size()));
            QVariantList matrixColumns = batchPairMatrixColumnsFromRows(batchRows);
            QVariantList matrixCells = batchPairMatrixCellsFromRows(batchRows);
            QVariantList timeRows;
            QVariantList plateauRows = batchPlateauRowsFromRows(batchRows);
            trimTable(stableRows, 200);
            trimTable(profitRows, 200);
            trimTable(symbolRows, 200);
            trimTable(pairRows, 200);
            trimTable(paramRows, 200);
            trimTable(plateauRows, 200);

            QJsonObject manifest;
            manifest.insert(QStringLiteral("type"), QStringLiteral("basis_chain_batch.result.v1"));
            manifest.insert(QStringLiteral("schema_version"), 1);
            manifest.insert(QStringLiteral("batch_id"), batchId);
            manifest.insert(QStringLiteral("strategy"), strategy);
            manifest.insert(QStringLiteral("pairs_evaluated"), static_cast<int>(prepared.size()));
            QJsonObject rowsObject;
            rowsObject.insert(QStringLiteral("path"), QStringLiteral("batch_rows.jsonl"));
            rowsObject.insert(QStringLiteral("row_schema"), QStringLiteral("object.v1"));
            manifest.insert(QStringLiteral("rows"), rowsObject);
            QJsonArray errors;
            if (!errorText.isEmpty() && errorText != QStringLiteral("cancelled")) {
                QJsonObject errorObject;
                errorObject.insert(QStringLiteral("message"), errorText);
                errors.push_back(errorObject);
            }
            manifest.insert(QStringLiteral("errors"), errors);
            writeJson(QDir(batchRoot).absoluteFilePath(QStringLiteral("manifest.json")), manifest);

            const QString summary = errorText == QStringLiteral("cancelled")
                ? QStringLiteral("Basis chain batch cancelled: %1 rows").arg(batchRows.size())
                : (!errorText.isEmpty()
                       ? QStringLiteral("Basis chain batch error: %1 (%2 rows)").arg(errorText).arg(batchRows.size())
                       : QStringLiteral("Basis chain batch complete: %1 pairs, %2 rows")
                             .arg(prepared.size())
                             .arg(batchRows.size()));

            QMetaObject::invokeMethod(this, [this,
                                             batchId,
                                             batchRoot,
                                             summary,
                                             stableRows = std::move(stableRows),
                                             profitRows = std::move(profitRows),
                                             symbolRows = std::move(symbolRows),
                                             pairRows = std::move(pairRows),
                                             paramRows = std::move(paramRows),
                                             skippedRows = std::move(skippedRows),
                                             summaryCards = std::move(summaryCards),
                                             matrixColumns = std::move(matrixColumns),
                                             matrixCells = std::move(matrixCells),
                                             timeRows = std::move(timeRows),
                                             plateauRows = std::move(plateauRows)] {
                setRunning_(false);
                setProgress_(100, summary);
                setStatusText_(summary);
                batchRunId_ = batchId;
                batchResultPath_ = batchRoot;
                batchSummaryText_ = summary;
                batchStableRows_ = stableRows;
                batchProfitRows_ = profitRows;
                batchSymbolRows_ = symbolRows;
                batchPairRows_ = pairRows;
                batchParamRows_ = paramRows;
                batchSkippedRows_ = skippedRows;
                batchSummaryCards_ = summaryCards;
                batchPairMatrixColumns_ = matrixColumns;
                batchPairMatrixCells_ = matrixCells;
                batchTimeRows_ = timeRows;
                batchPlateauRows_ = plateauRows;
                emit batchResultsChanged();
                refresh();
                reloadSessions();
            }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            const QString status = QStringLiteral("Basis chain batch crashed: ") + QString::fromUtf8(ex.what());
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                batchSummaryText_ = status;
                emit batchResultsChanged();
                refresh();
            }, Qt::QueuedConnection);
        } catch (...) {
            const QString status = QStringLiteral("Basis chain batch crashed: unknown exception");
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                batchSummaryText_ = status;
                emit batchResultsChanged();
                refresh();
            }, Qt::QueuedConnection);
        }
    });
}

void BacktestViewModel::startBatchSweep() {
    if (running_) return;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr || !strategyMetadataSupportsSessionCount(*metadata, 2)) {
        setStatusText_(QStringLiteral("Selected strategy does not support 2-leg batch sweep"));
        return;
    }

    std::vector<hft_backtest::BacktestSweepParamRange> ranges;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        if (normalizedParamMode(paramModes_.value(key, QStringLiteral("fixed"))) == QStringLiteral("fixed")) continue;
        bool minOk = false;
        bool maxOk = false;
        bool stepOk = false;
        const qint64 minRaw = paramMinValues_.value(key).trimmed().toLongLong(&minOk);
        const qint64 maxRaw = paramMaxValues_.value(key).trimmed().toLongLong(&maxOk);
        const qint64 stepRaw = paramStepValues_.value(key).trimmed().toLongLong(&stepOk);
        if (!minOk || !maxOk || !stepOk || stepRaw <= 0 || maxRaw < minRaw) {
            setStatusText_(QStringLiteral("Invalid sweep range for %1").arg(key));
            return;
        }
        hft_backtest::BacktestSweepParamRange range{};
        range.key = key.toStdString();
        range.minRaw = minRaw;
        range.maxRaw = maxRaw;
        range.stepRaw = stepRaw;
        range.mode = hft_backtest::BacktestSweepParamMode::Grid;
        ranges.push_back(std::move(range));
    }
    if (ranges.empty()) {
        setStatusText_(QStringLiteral("Choose at least one Sweep parameter"));
        return;
    }

    const QStringList universePaths = batchUniverseSessionPaths_();
    QVector<BatchSweepSessionInfo> sessions;
    sessions.reserve(universePaths.size());
    for (const QString& path : universePaths) sessions.push_back(sessionInfoForPath(path));

    QVariantList skipped;
    const int maxPairs = static_cast<int>(latencyValue_(batchPairBudget_, 64));
    const QVector<BatchSweepPair> pairs = buildBatchSweepPairs(sessions, maxPairs, batchOnlyFutures_, &skipped);
    if (pairs.empty()) {
        batchSkippedRows_ = skipped;
        batchStableRows_.clear();
        batchProfitRows_.clear();
        batchSymbolRows_.clear();
        batchPairRows_.clear();
        batchParamRows_.clear();
        batchSummaryCards_ = batchSummaryCardsFromRows(QVariantList{}, skipped, 0, 0);
        batchPairMatrixColumns_.clear();
        batchPairMatrixCells_.clear();
        batchTimeRows_.clear();
        batchPlateauRows_.clear();
        batchSummaryText_ = batchOnlyFutures_
            ? QStringLiteral("No futures same-symbol exchange pairs")
            : QStringLiteral("No same-symbol exchange pairs");
        emit batchResultsChanged();
        setStatusText_(batchSummaryText_);
        return;
    }

    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);
    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting batch sweep"));
    setStatusText_(QStringLiteral("Batch sweep running"));

    const QString batchId = QStringLiteral("batch-") + runId_();
    const QString batchRoot = QDir(pairs.front().first.path).absoluteFilePath(QStringLiteral("backtests/batches/%1").arg(batchId));
    QDir().mkpath(batchRoot);

    const quint64 pingLatency = latencyValue_(pingLatencyUs_, 1000);
    const quint64 latencySeed = latencyValue_(latencySeed_, 0);
    const quint64 searchSeed = latencyValue_(sweepSeed_, 0);
    const quint64 runBudget = latencyValue_(sweepBudget_, 64);
    const quint64 marketDataLatency = latencyValue_(marketDataLatencyUs_, 250);
    const quint64 marketDataJitter = latencyValue_(marketDataJitterUs_, 100);
    const quint64 marketOrderLatency = latencyValue_(marketOrderLatencyUs_, pingLatency);
    const quint64 marketOrderJitter = latencyValue_(marketOrderJitterUs_, 0);
    const quint64 limitOrderLatency = latencyValue_(limitOrderLatencyUs_, pingLatency);
    const quint64 limitOrderJitter = latencyValue_(limitOrderJitterUs_, 0);
    const quint64 cancelOrderLatency = latencyValue_(cancelOrderLatencyUs_, limitOrderLatency);
    const quint64 cancelOrderJitter = latencyValue_(cancelOrderJitterUs_, limitOrderJitter);
    const quint64 userDataLatency = latencyValue_(userDataLatencyUs_, 0);
    const quint64 userDataJitter = latencyValue_(userDataJitterUs_, 0);
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const bool rateLimitsEnabled = rateLimitsEnabled_;
    const QString strategy = selectedStrategy_;
    const QString indicatorProfile = selectedIndicatorProfile_;

    std::vector<PreparedBatchPair> prepared;
    prepared.reserve(static_cast<std::size_t>(pairs.size()));
    QString firstConfigError;
    for (const BatchSweepPair& pair : pairs) {
        const QStringList sessionPaths{pair.first.path, pair.second.path};
        const QString pairSlug = cleanPairSlug(pair);
        const RunConfigWriteResult config = writeRunConfigForSessionPaths_(QStringLiteral("batches/%1/pair_configs/%2").arg(batchId, pairSlug),
                                                                           sessionPaths,
                                                                           {},
                                                                           true,
                                                                           false);
        if (!config.ok()) {
            if (firstConfigError.isEmpty()) firstConfigError = config.error;
            QVariantMap row;
            row.insert(QStringLiteral("reason"), QStringLiteral("failed to write pair config: %1").arg(config.error));
            row.insert(QStringLiteral("symbol"), pair.first.symbol);
            row.insert(QStringLiteral("exchangePair"), batchExchangePairLabel(pair));
            skipped.push_back(row);
            continue;
        }

        PreparedBatchPair item;
        item.pair = pair;
        item.configPath = config.path;
        item.outputPath = QDir(batchRoot).absoluteFilePath(QStringLiteral("sweeps/%1").arg(pairSlug));
        item.request.baseRun.sessionPath = pair.first.path.toStdString();
        hft_backtest::BacktestSessionRequest leg;
        leg.path = pair.second.path.toStdString();
        leg.venue = pair.second.venue.toStdString();
        leg.symbol = pair.second.symbol.toStdString();
        item.request.baseRun.sessions.push_back(std::move(leg));
        item.request.baseRun.configPath = item.configPath.toStdString();
        item.request.baseRun.strategy = strategy.toStdString();
        item.request.baseRun.indicatorProfile = indicatorProfile.toStdString();
        item.request.baseRun.latencySeed = latencySeed;
        item.request.baseRun.marketDataLatency.baseUs = marketDataLatency;
        item.request.baseRun.marketDataLatency.jitterUs = marketDataJitter;
        item.request.baseRun.marketOrderLatency.baseUs = marketOrderLatency;
        item.request.baseRun.marketOrderLatency.jitterUs = marketOrderJitter;
        item.request.baseRun.limitOrderLatency.baseUs = limitOrderLatency;
        item.request.baseRun.limitOrderLatency.jitterUs = limitOrderJitter;
        item.request.baseRun.cancelOrderLatency.baseUs = cancelOrderLatency;
        item.request.baseRun.cancelOrderLatency.jitterUs = cancelOrderJitter;
        item.request.baseRun.userDataLatency.baseUs = userDataLatency;
        item.request.baseRun.userDataLatency.jitterUs = userDataJitter;
        item.request.baseRun.orderLatencyUs = marketOrderLatency;
        item.request.baseRun.cancelLatencyUs = cancelOrderLatency;
        item.request.baseRun.initialBalanceE8 = initialBalance;
        const std::vector<QVariantMap> venueRows = venueExecutionRowsForPaths_(sessionPaths);
        for (const QVariantMap& row : venueRows) {
            item.request.baseRun.legInitialBalancesE8.push_back(decimalE8Value_(row.value(QStringLiteral("initialBalanceUsdt")).toString(), initialBalance));
            item.request.baseRun.feeSchedules.push_back(feeScheduleFromVenueRow(row));
            hft_backtest::BacktestLatencySchedule latency;
            latency.exchange = row.value(QStringLiteral("exchange")).toString().toStdString();
            latency.market = row.value(QStringLiteral("market")).toString().toStdString();
            latency.marketData.baseUs = latencyValue_(row.value(QStringLiteral("marketDataLatencyUs")).toString(), marketDataLatency);
            latency.marketData.jitterUs = latencyValue_(row.value(QStringLiteral("marketDataJitterUs")).toString(), marketDataJitter);
            latency.marketOrder.baseUs = latencyValue_(row.value(QStringLiteral("marketOrderLatencyUs")).toString(), marketOrderLatency);
            latency.marketOrder.jitterUs = latencyValue_(row.value(QStringLiteral("marketOrderJitterUs")).toString(), marketOrderJitter);
            latency.limitOrder.baseUs = latencyValue_(row.value(QStringLiteral("limitOrderLatencyUs")).toString(), limitOrderLatency);
            latency.limitOrder.jitterUs = latencyValue_(row.value(QStringLiteral("limitOrderJitterUs")).toString(), limitOrderJitter);
            latency.cancelOrder.baseUs = latencyValue_(row.value(QStringLiteral("cancelOrderLatencyUs")).toString(), cancelOrderLatency);
            latency.cancelOrder.jitterUs = latencyValue_(row.value(QStringLiteral("cancelOrderJitterUs")).toString(), cancelOrderJitter);
            latency.userData.baseUs = latencyValue_(row.value(QStringLiteral("userDataLatencyUs")).toString(), userDataLatency);
            latency.userData.jitterUs = latencyValue_(row.value(QStringLiteral("userDataJitterUs")).toString(), userDataJitter);
            item.request.baseRun.latencySchedules.push_back(std::move(latency));
            hft_backtest::BacktestRateLimitSchedule rateLimit = rateLimitScheduleFromVenueRow(row);
            if (!rateLimit.buckets.empty() || !rateLimit.actions.empty()) item.request.baseRun.rateLimitSchedules.push_back(std::move(rateLimit));
        }
        item.request.baseRun.rateLimitsEnabled = rateLimitsEnabled;
        item.request.baseRun.writeArtifacts = false;
        item.request.sweepId = QStringLiteral("%1-%2").arg(batchId, pairSlug).toStdString();
        item.request.runBudget = runBudget;
        item.request.searchSeed = searchSeed;
        item.request.outputPath = item.outputPath.toStdString();
        item.request.ranges = ranges;
        prepared.push_back(std::move(item));
    }

    if (prepared.empty()) {
        setRunning_(false);
        setProgress_(100, QStringLiteral("Batch sweep failed"));
        batchSkippedRows_ = skipped;
        batchSummaryCards_ = batchSummaryCardsFromRows(QVariantList{}, skipped, 0, 0);
        batchPairMatrixColumns_.clear();
        batchPairMatrixCells_.clear();
        batchTimeRows_.clear();
        batchPlateauRows_.clear();
        batchSummaryText_ = firstConfigError.isEmpty()
            ? QStringLiteral("No runnable pairs after config generation")
            : QStringLiteral("No runnable pairs after config generation: %1").arg(firstConfigError);
        emit batchResultsChanged();
        setStatusText_(batchSummaryText_);
        return;
    }

    configureWorkerThreadStack_();
    worker_ = std::thread([this, prepared = std::move(prepared), skipped = std::move(skipped), batchId, batchRoot, strategy, runBudget, maxPairs] {
        try {
            QVariantList batchRows;
            QVariantList batchCurves;
            QVariantList skippedRows = skipped;
            QString errorText;
            const QString rowsPath = QDir(batchRoot).absoluteFilePath(QStringLiteral("batch_rows.jsonl"));
            const QString curvesPath = QDir(batchRoot).absoluteFilePath(QStringLiteral("batch_curves.jsonl"));
            QFile rowsFile(rowsPath);
            QFile curvesFile(curvesPath);
            if (!rowsFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                errorText = QStringLiteral("failed to write batch rows");
            } else if (!curvesFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                errorText = QStringLiteral("failed to write batch curves");
            }

            std::uint64_t pointsEvaluated = 0;
            for (int i = 0; errorText.isEmpty() && i < static_cast<int>(prepared.size()); ++i) {
                if (workerCancelRequested()) {
                    errorText = QStringLiteral("cancelled");
                    break;
                }
                const PreparedBatchPair& pair = prepared[static_cast<std::size_t>(i)];
                BatchProgressContext progressCtx{this, i, static_cast<int>(prepared.size()), batchExchangePairLabel(pair.pair)};
                const hft_backtest::BacktestSweepResult result = hft_backtest::runBacktestSweep(pair.request, batchProgressCallback, &progressCtx);
                pointsEvaluated += result.pointsEvaluated;
                if (!hft_backtest::isOk(result.status)) {
                    const auto statusText = hft_backtest::statusToString(result.status);
                    QVariantMap row;
                    row.insert(QStringLiteral("reason"), QStringLiteral("sweep failed"));
                    row.insert(QStringLiteral("status"), QString::fromUtf8(statusText.data(), static_cast<qsizetype>(statusText.size())));
                    row.insert(QStringLiteral("error"), QString::fromStdString(result.error));
                    row.insert(QStringLiteral("symbol"), pair.pair.first.symbol);
                    row.insert(QStringLiteral("exchangePair"), batchExchangePairLabel(pair.pair));
                    skippedRows.push_back(row);
                    continue;
                }
                for (const QVariantMap& sweepRow : readJsonlMaps(QString::fromStdString(result.rowsPath.generic_string()))) {
                    const QVariantMap out = enrichedBatchRow(sweepRow, pair, result, batchId);
                    batchRows.push_back(out);
                    if (!appendJsonl(rowsFile, out)) {
                        errorText = QStringLiteral("failed to append batch row");
                        break;
                    }
                }
                if (!errorText.isEmpty()) break;
                for (const QVariantMap& sweepCurve : readJsonlMaps(QString::fromStdString(result.curvesPath.generic_string()))) {
                    const QVariantMap out = enrichedBatchRow(sweepCurve, pair, result, batchId);
                    batchCurves.push_back(out);
                    if (!appendJsonl(curvesFile, out)) {
                        errorText = QStringLiteral("failed to append batch curve");
                        break;
                    }
                }
            }
            rowsFile.close();
            curvesFile.close();

            QVariantList stableRows = batchStableRowsFromRows(batchRows);
            QVariantList profitRows = batchProfitRowsFromRows(batchRows);
            QVariantList symbolRows = batchSymbolRowsFromRows(batchRows);
            QVariantList pairRows = batchPairRowsFromRows(batchRows);
            QVariantList paramRows = batchParamRowsFromRows(batchRows);
            QVariantList summaryCards = batchSummaryCardsFromRows(batchRows, skippedRows, static_cast<int>(prepared.size()), pointsEvaluated);
            QVariantList matrixColumns = batchPairMatrixColumnsFromRows(batchRows);
            QVariantList matrixCells = batchPairMatrixCellsFromRows(batchRows);
            QVariantList timeRows = batchTimeRowsFromCurves(batchCurves);
            QVariantList plateauRows = batchPlateauRowsFromRows(batchRows);
            trimTable(stableRows, 200);
            trimTable(profitRows, 200);
            trimTable(symbolRows, 200);
            trimTable(pairRows, 200);
            trimTable(paramRows, 200);
            trimTable(timeRows, 200);
            trimTable(plateauRows, 200);

            QJsonObject manifest;
            manifest.insert(QStringLiteral("type"), QStringLiteral("batch_sweep.result.v1"));
            manifest.insert(QStringLiteral("schema_version"), 1);
            manifest.insert(QStringLiteral("batch_id"), batchId);
            manifest.insert(QStringLiteral("strategy"), strategy);
            manifest.insert(QStringLiteral("pair_budget"), maxPairs);
            manifest.insert(QStringLiteral("sweep_budget_per_pair"), static_cast<qint64>(runBudget));
            manifest.insert(QStringLiteral("pairs_evaluated"), static_cast<int>(prepared.size()));
            manifest.insert(QStringLiteral("points_evaluated"), static_cast<qint64>(pointsEvaluated));
            QJsonObject rowsObject;
            rowsObject.insert(QStringLiteral("path"), QStringLiteral("batch_rows.jsonl"));
            rowsObject.insert(QStringLiteral("row_schema"), QStringLiteral("object.v1"));
            manifest.insert(QStringLiteral("rows"), rowsObject);
            QJsonObject curvesObject;
            curvesObject.insert(QStringLiteral("path"), QStringLiteral("batch_curves.jsonl"));
            curvesObject.insert(QStringLiteral("row_schema"), QStringLiteral("object.v1"));
            manifest.insert(QStringLiteral("curves"), curvesObject);
            QJsonArray errors;
            if (!errorText.isEmpty() && errorText != QStringLiteral("cancelled")) {
                QJsonObject errorObject;
                errorObject.insert(QStringLiteral("message"), errorText);
                errors.push_back(errorObject);
            }
            manifest.insert(QStringLiteral("errors"), errors);
            writeJson(QDir(batchRoot).absoluteFilePath(QStringLiteral("manifest.json")), manifest);

            const QString summary = errorText == QStringLiteral("cancelled")
                ? QStringLiteral("Batch cancelled: %1 rows").arg(batchRows.size())
                : (!errorText.isEmpty()
                       ? QStringLiteral("Batch error: %1 (%2 rows)").arg(errorText).arg(batchRows.size())
                       : QStringLiteral("Batch complete: %1 pairs, %2 points, %3 rows")
                             .arg(prepared.size())
                             .arg(static_cast<qulonglong>(pointsEvaluated))
                             .arg(batchRows.size()));

            QMetaObject::invokeMethod(this, [this,
                                             batchId,
                                             batchRoot,
                                             summary,
                                             stableRows = std::move(stableRows),
                                             profitRows = std::move(profitRows),
                                             symbolRows = std::move(symbolRows),
                                             pairRows = std::move(pairRows),
                                             paramRows = std::move(paramRows),
                                             skippedRows = std::move(skippedRows),
                                             summaryCards = std::move(summaryCards),
                                             matrixColumns = std::move(matrixColumns),
                                             matrixCells = std::move(matrixCells),
                                             timeRows = std::move(timeRows),
                                             plateauRows = std::move(plateauRows)] {
                setRunning_(false);
                setProgress_(100, summary);
                setStatusText_(summary);
                batchRunId_ = batchId;
                batchResultPath_ = batchRoot;
                batchSummaryText_ = summary;
                batchStableRows_ = stableRows;
                batchProfitRows_ = profitRows;
                batchSymbolRows_ = symbolRows;
                batchPairRows_ = pairRows;
                batchParamRows_ = paramRows;
                batchSkippedRows_ = skippedRows;
                batchSummaryCards_ = summaryCards;
                batchPairMatrixColumns_ = matrixColumns;
                batchPairMatrixCells_ = matrixCells;
                batchTimeRows_ = timeRows;
                batchPlateauRows_ = plateauRows;
                emit batchResultsChanged();
                refresh();
                reloadSessions();
            }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            const QString status = QStringLiteral("Batch crashed: ") + QString::fromUtf8(ex.what());
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                batchSummaryText_ = status;
                emit batchResultsChanged();
                refresh();
            }, Qt::QueuedConnection);
        } catch (...) {
            const QString status = QStringLiteral("Batch crashed: unknown exception");
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                batchSummaryText_ = status;
                emit batchResultsChanged();
                refresh();
            }, Qt::QueuedConnection);
        }
    });
}

}  // namespace hftrec::gui
