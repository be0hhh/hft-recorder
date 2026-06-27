#include "gui/backtests/BacktestViewModel.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QVariantMap>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "hft_backtest/backtest.hpp"
#include "hft_backtest/backtest_sweep.hpp"
#include "core/common/Status.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "gui/backtests/BacktestSessionSummary.hpp"
#include "gui/backtests/BacktestExecutionConfigHelpers.hpp"
#include "gui/backtests/BacktestResultHelpers.hpp"
#include "gui/backtests/BacktestSessionHelpers.hpp"
#include "gui/backtests/BacktestStrategyConfigHelpers.hpp"
#include "gui/backtests/BacktestSweepHelpers.hpp"

namespace hftrec::gui {
namespace {

QString statusTextFor(hft_backtest::Status status, const std::string& error = {}, const std::vector<std::string>& warnings = {}) {
    if (status == hft_backtest::Status::Ok) {
        if (!warnings.empty()) return QStringLiteral("Backtest complete: warning: ") + QString::fromStdString(warnings.front());
        return QStringLiteral("Backtest complete");
    }
    if (status == hft_backtest::Status::Cancelled) return QStringLiteral("Backtest cancelled");
    const std::string_view statusText = hft_backtest::statusToString(status);
    QString message = QStringLiteral("Backtest failed: %1")
        .arg(QString::fromUtf8(statusText.data(), static_cast<qsizetype>(statusText.size())));
    if (!error.empty()) message += QStringLiteral(": ") + QString::fromStdString(error);
    return message;
}

QString sweepStatusTextFor(hft_backtest::Status status, const std::string& error = {}) {
    if (status == hft_backtest::Status::Ok) return QStringLiteral("Sweep complete");
    if (status == hft_backtest::Status::Cancelled) return QStringLiteral("Sweep cancelled");
    const std::string_view statusText = hft_backtest::statusToString(status);
    QString message = QStringLiteral("Sweep failed: %1")
        .arg(QString::fromUtf8(statusText.data(), static_cast<qsizetype>(statusText.size())));
    if (!error.empty()) message += QStringLiteral(": ") + QString::fromStdString(error);
    return message;
}

bool progressCallback(const hft_backtest::BacktestProgress& progress, void* userData) noexcept {
    auto* vm = static_cast<BacktestViewModel*>(userData);
    if (vm == nullptr) return false;
    const QString text = QStringLiteral("%1: %2/%3 events")
        .arg(QString::fromStdString(progress.stage))
        .arg(static_cast<qulonglong>(progress.eventsDone))
        .arg(static_cast<qulonglong>(progress.eventsTotal));
    QMetaObject::invokeMethod(vm, [vm, percent = static_cast<int>(progress.percent), text] {
        vm->applyWorkerProgress(percent, text);
    }, Qt::QueuedConnection);
    return !vm->workerCancelRequested();
}

bool sessionVenueIsKnown(const QString& sessionPath, QString* reason) {
    const QString exchange = manifestValue(sessionPath, QStringLiteral("exchange")).trimmed().toLower();
    const QString market = manifestValue(sessionPath, QStringLiteral("market")).trimmed().toLower();
    if (exchange.isEmpty() || market.isEmpty() || !isVenueSectionKnown(exchange, market)) {
        const auto printableExchange = exchange.isEmpty() ? QStringLiteral("<empty>") : exchange;
        const auto printableMarket = market.isEmpty() ? QStringLiteral("<empty>") : market;
        if (reason != nullptr) {
            *reason = QStringLiteral("Unsupported venue: exchange=%1 market=%2 in session=%3")
                          .arg(printableExchange, printableMarket, sessionPath);
        }
        return false;
    }
    return true;
}

}  // namespace

void BacktestViewModel::configureWorkerThreadStack_() noexcept {
#if defined(__linux__)
    static std::once_flag once;
    std::call_once(once, [] {
        constexpr std::size_t kBacktestWorkerStackBytes = 64u * 1024u * 1024u;
        pthread_attr_t attr{};
        if (pthread_getattr_default_np(&attr) != 0) return;
        std::size_t stackSize = 0;
        if (pthread_attr_getstacksize(&attr, &stackSize) == 0 && stackSize < kBacktestWorkerStackBytes) {
            (void)pthread_attr_setstacksize(&attr, kBacktestWorkerStackBytes);
            (void)pthread_setattr_default_np(&attr);
        }
        (void)pthread_attr_destroy(&attr);
    });
#endif
}

BacktestViewModel::BacktestViewModel(QObject* parent) : QObject(parent) {
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(200);
    connect(&refreshTimer_, &QTimer::timeout, this, &BacktestViewModel::refresh);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this]() { scheduleRefresh_(); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this]() { scheduleRefresh_(); });
    loadPersistentConfig_();
    sessions_ = loadSessions_();
    selectedSessionId_ = firstSelectableSessionId(sessions_);
    ensureSelectedStrategySupportsSessionCount_();
    refresh();
}

BacktestViewModel::~BacktestViewModel() {
    cancelBacktest();
    stopWorker_();
}

QString BacktestViewModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

QVariantList BacktestViewModel::sessions() const {
    return sessions_;
}

QVariantList BacktestViewModel::loadSessions_() const {
    QVariantList out;
    const QString root = recordingsRoot();
    const auto backtestCountsBySession = backtestLegCountsBySession(root);
    const auto discovery = hftrec::recordings::discoverRecordings(root.toStdString());
    for (const auto& group : discovery.groups) {
        QVariantList groupPaths;
        int groupFirstLegBacktests = 0;
        int groupSecondLegBacktests = 0;
        for (const auto& session : group.sessions) {
            const QString sessionId = QString::fromStdString(session.sessionId);
            groupPaths.push_back(QString::fromStdString(session.path.string()));
            const BacktestLegCounts counts = backtestCountsBySession.value(sessionId);
            groupFirstLegBacktests += counts.firstLeg;
            groupSecondLegBacktests += counts.secondLeg;
        }
        if (!group.sessions.empty()) {
            QVariantMap groupRow;
            groupRow.insert(QStringLiteral("id"), QStringLiteral("group:%1").arg(QString::fromStdString(group.id)));
            groupRow.insert(QStringLiteral("label"), QString::fromStdString(group.title));
            groupRow.insert(QStringLiteral("path"), QString::fromStdString(group.sessions.front().path.string()));
            groupRow.insert(QStringLiteral("sessionPaths"), groupPaths);
            groupRow.insert(QStringLiteral("hasManifest"), true);
            groupRow.insert(QStringLiteral("hasBacktests"), groupFirstLegBacktests > 0 || groupSecondLegBacktests > 0);
            groupRow.insert(QStringLiteral("backtestCount"), groupFirstLegBacktests);
            groupRow.insert(QStringLiteral("firstLegBacktestCount"), groupFirstLegBacktests);
            groupRow.insert(QStringLiteral("secondLegBacktestCount"), groupSecondLegBacktests);
            groupRow.insert(QStringLiteral("isGroup"), true);
            groupRow.insert(QStringLiteral("selectable"), false);
            groupRow.insert(QStringLiteral("groupId"), QString::fromStdString(group.id));
            groupRow.insert(QStringLiteral("rightText"), QStringLiteral("%1 legs | rows %2 | 1BT %3 | 2BT %4")
                                                    .arg(group.sessions.size())
                                                    .arg(QString::number(static_cast<qulonglong>(group.totalRows)))
                                                    .arg(groupFirstLegBacktests)
                                                    .arg(groupSecondLegBacktests));
            out.push_back(groupRow);
        }

        for (const auto& session : group.sessions) {
            const QString id = QString::fromStdString(session.sessionId);
            const QString path = QString::fromStdString(session.path.string());
            const QDir backtestsDir(QDir(path).absoluteFilePath(QStringLiteral("backtests")));
            const BacktestLegCounts backtestCounts = backtestCountsBySession.value(id);
            QVariantMap row;
            row.insert(QStringLiteral("id"), id);
            row.insert(QStringLiteral("label"), QStringLiteral("%1/%2 %3")
                                            .arg(QString::fromStdString(session.exchange),
                                                 QString::fromStdString(session.market),
                                                 QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front())));
            row.insert(QStringLiteral("path"), path);
            row.insert(QStringLiteral("sessionPaths"), QVariantList{path});
            row.insert(QStringLiteral("hasManifest"), QFileInfo::exists(QDir(path).absoluteFilePath(QStringLiteral("manifest.json"))));
            row.insert(QStringLiteral("hasBacktests"), backtestsDir.exists());
            row.insert(QStringLiteral("backtestCount"), backtestCounts.firstLeg);
            row.insert(QStringLiteral("firstLegBacktestCount"), backtestCounts.firstLeg);
            row.insert(QStringLiteral("secondLegBacktestCount"), backtestCounts.secondLeg);
            row.insert(QStringLiteral("isGroup"), false);
            row.insert(QStringLiteral("selectable"), true);
            row.insert(QStringLiteral("parentGroupId"), QString::fromStdString(group.id));
            row.insert(QStringLiteral("rightText"), sessionSourceSummary(path, backtestCounts));
            out.push_back(row);
        }
    }
    return out;
}

QString BacktestViewModel::selectedSessionPath() const {
    if (!manualSessionPath_.trimmed().isEmpty()) return manualSessionPath_;
    if (selectedSessionId_.trimmed().isEmpty()) return {};
    const QVariantMap row = sessionRowById(sessions_, selectedSessionId_);
    const QStringList paths = sessionPathsFromRow(row);
    if (!paths.empty()) return paths.front();
    return sessionPathFromToken(recordingsRoot(), selectedSessionId_);
}

QStringList BacktestViewModel::selectedSessionPaths_() const {
    QStringList out;
    if (!manualSessionPath_.trimmed().isEmpty()) {
        out.push_back(manualSessionPath_);
    } else {
        const QStringList primaryPaths = sessionPathsFromRow(sessionRowById(sessions_, selectedSessionId_));
        for (const QString& path : primaryPaths) {
            if (!path.trimmed().isEmpty() && !out.contains(path)) out.push_back(path);
            if (out.size() >= 8) break;
        }
        if (out.empty()) {
            const QString primary = selectedSessionPath();
            if (!primary.trimmed().isEmpty()) out.push_back(primary);
        }
    }
    const QStringList tokens = extraSessionIds_.split(QRegularExpression(QStringLiteral("[,;\\n]+")), Qt::SkipEmptyParts);
    const QString root = recordingsRoot();
    for (const QString& token : tokens) {
        const QStringList rowPaths = sessionPathsFromRow(sessionRowById(sessions_, token.trimmed()));
        const QStringList paths = rowPaths.empty() ? QStringList{sessionPathFromToken(root, token)} : rowPaths;
        for (const QString& path : paths) {
            if (path.isEmpty() || out.contains(path)) continue;
            out.push_back(path);
            if (out.size() >= 8) break;
        }
        if (out.size() >= 8) break;
    }
    return out;
}

QStringList BacktestViewModel::orderedSessionPathsForRun_() const {
    QStringList paths = selectedSessionPaths_();
    if (selectedStrategy_ != QStringLiteral("basis_convergence_probe") || paths.size() != 2) return paths;

    int spotIndex = -1;
    int futuresIndex = -1;
    for (int i = 0; i < paths.size(); ++i) {
        const QString market = manifestValue(paths.at(i), QStringLiteral("market")).trimmed().toLower();
        if (market == QStringLiteral("spot")) {
            spotIndex = i;
        } else if (market == QStringLiteral("futures") ||
                   market == QStringLiteral("future") ||
                   market == QStringLiteral("forts") ||
                   market == QStringLiteral("usdt") ||
                   market == QStringLiteral("usdc") ||
                   market == QStringLiteral("linear")) {
            futuresIndex = i;
        }
    }
    if (spotIndex < 0 || futuresIndex < 0 || spotIndex == futuresIndex) return paths;
    return QStringList{paths.at(spotIndex), paths.at(futuresIndex)};
}

QVariantList BacktestViewModel::selectedSessionLegs() const {
    QVariantList out;
    const QStringList paths = selectedSessionPaths_();
    for (int i = 0; i < paths.size(); ++i) {
        QVariantMap row;
        const QString path = paths.at(i);
        const QString venueKey = venueExecutionKey(path);
        const QString makerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("maker_fee_bps"));
        const QString takerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("taker_fee_bps"));
        row.insert(QStringLiteral("index"), i);
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("id"), sessionIdFromPath_(path));
        row.insert(QStringLiteral("symbol"), symbolForSessionPath(path));
        row.insert(QStringLiteral("venue"), venueSectionForSession(path));
        row.insert(QStringLiteral("venueKey"), venueKey);
        row.insert(QStringLiteral("exchange"), manifestValue(path, QStringLiteral("exchange")).trimmed().toLower());
        row.insert(QStringLiteral("market"), normalizedFeeMarket(manifestValue(path, QStringLiteral("market"))));
        row.insert(QStringLiteral("initialBalanceUsdt"), venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_));
        if (!makerFeeOverride.isEmpty()) row.insert(QStringLiteral("makerFeeBps"), makerFeeOverride);
        if (!takerFeeOverride.isEmpty()) row.insert(QStringLiteral("takerFeeBps"), takerFeeOverride);
        row.insert(QStringLiteral("executionPresetSummary"),
                   exchangeExecutionPresetSummary(row.value(QStringLiteral("exchange")).toString(),
                                                  row.value(QStringLiteral("market")).toString(),
                                                  rateLimitsEnabled_));
        row.insert(QStringLiteral("marketDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_latency_us"), marketDataLatencyUs_));
        row.insert(QStringLiteral("marketDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_jitter_us"), marketDataJitterUs_));
        row.insert(QStringLiteral("marketOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_latency_us"), marketOrderLatencyUs_));
        row.insert(QStringLiteral("marketOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_jitter_us"), marketOrderJitterUs_));
        row.insert(QStringLiteral("limitOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_latency_us"), limitOrderLatencyUs_));
        row.insert(QStringLiteral("limitOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_jitter_us"), limitOrderJitterUs_));
        row.insert(QStringLiteral("cancelOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_latency_us"), cancelOrderLatencyUs_));
        row.insert(QStringLiteral("cancelOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_jitter_us"), cancelOrderJitterUs_));
        row.insert(QStringLiteral("userDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_latency_us"), userDataLatencyUs_));
        row.insert(QStringLiteral("userDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_jitter_us"), userDataJitterUs_));
        row.insert(QStringLiteral("label"), QStringLiteral("%1: %2 %3")
            .arg(i + 1)
            .arg(venueSectionForSession(path), symbolForSessionPath(path)));
        out.push_back(row);
    }
    return out;
}

int BacktestViewModel::selectedSessionCount() const {
    return selectedSessionPaths_().size();
}

QString BacktestViewModel::venueExecutionValue_(const QString& venueKey, const QString& field, const QString& fallback) const {
    const QString normalizedField = field.trimmed().toLower();
    if (venueKey.isEmpty() || normalizedField.isEmpty()) return fallback;
    const QString mapKey = venueExecutionMapKey(venueKey, normalizedField);
    if (venueExecutionValues_.contains(mapKey)) return venueExecutionValues_.value(mapKey);
    return settings_.value(QStringLiteral("backtests/venue_execution/%1/%2")
                               .arg(venueExecutionSettingKey(venueKey), normalizedField),
                           fallback)
        .toString()
        .trimmed();
}

QString BacktestViewModel::venueExecutionOverrideValue_(const QString& venueKey, const QString& field) const {
    const QString normalizedField = field.trimmed().toLower();
    if (venueKey.isEmpty() || normalizedField.isEmpty()) return {};
    const QString mapKey = venueExecutionMapKey(venueKey, normalizedField);
    if (venueExecutionValues_.contains(mapKey)) return venueExecutionValues_.value(mapKey).trimmed();
    const QString settingsKey = QStringLiteral("backtests/venue_execution/%1/%2")
                                    .arg(venueExecutionSettingKey(venueKey), normalizedField);
    if (!settings_.contains(settingsKey)) return {};
    return settings_.value(settingsKey).toString().trimmed();
}

std::vector<QVariantMap> BacktestViewModel::venueExecutionRowsForPaths_(const QStringList& paths) const {
    std::vector<QVariantMap> out;
    QSet<QString> emitted;
    out.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) {
        const QString venueKey = venueExecutionKey(path);
        if (venueKey.isEmpty() || emitted.contains(venueKey)) continue;
        emitted.insert(venueKey);
        const QString makerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("maker_fee_bps"));
        const QString takerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("taker_fee_bps"));
        QVariantMap row;
        row.insert(QStringLiteral("exchange"), manifestValue(path, QStringLiteral("exchange")).trimmed().toLower());
        row.insert(QStringLiteral("market"), normalizedFeeMarket(manifestValue(path, QStringLiteral("market"))));
        row.insert(QStringLiteral("initialBalanceUsdt"), venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_));
        if (!makerFeeOverride.isEmpty()) row.insert(QStringLiteral("makerFeeBps"), makerFeeOverride);
        if (!takerFeeOverride.isEmpty()) row.insert(QStringLiteral("takerFeeBps"), takerFeeOverride);
        row.insert(QStringLiteral("marketDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_latency_us"), marketDataLatencyUs_));
        row.insert(QStringLiteral("marketDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_data_jitter_us"), marketDataJitterUs_));
        row.insert(QStringLiteral("marketOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_latency_us"), marketOrderLatencyUs_));
        row.insert(QStringLiteral("marketOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("market_order_jitter_us"), marketOrderJitterUs_));
        row.insert(QStringLiteral("limitOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_latency_us"), limitOrderLatencyUs_));
        row.insert(QStringLiteral("limitOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("limit_order_jitter_us"), limitOrderJitterUs_));
        row.insert(QStringLiteral("cancelOrderLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_latency_us"), cancelOrderLatencyUs_));
        row.insert(QStringLiteral("cancelOrderJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("cancel_order_jitter_us"), cancelOrderJitterUs_));
        row.insert(QStringLiteral("userDataLatencyUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_latency_us"), userDataLatencyUs_));
        row.insert(QStringLiteral("userDataJitterUs"), venueExecutionValue_(venueKey, QStringLiteral("user_data_jitter_us"), userDataJitterUs_));
        row.insert(QStringLiteral("rateLimitOrdersLimit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_orders_limit"), QString{}));
        row.insert(QStringLiteral("rateLimitOrdersIntervalMs"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_orders_interval_ms"), QString{}));
        row.insert(QStringLiteral("rateLimitCancelOrdersLimit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_orders_limit"), QString{}));
        row.insert(QStringLiteral("rateLimitCancelOrdersIntervalMs"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_orders_interval_ms"), QString{}));
        row.insert(QStringLiteral("rateLimitReduceOnlyOrdersLimit"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_orders_limit"), QString{}));
        row.insert(QStringLiteral("rateLimitReduceOnlyOrdersIntervalMs"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_orders_interval_ms"), QString{}));
        row.insert(QStringLiteral("rateLimitLimitOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_limit_order_cost"), QStringLiteral("1")));
        row.insert(QStringLiteral("rateLimitMarketOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_market_order_cost"), QStringLiteral("1")));
        row.insert(QStringLiteral("rateLimitCancelOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_cancel_order_cost"), QStringLiteral("1")));
        row.insert(QStringLiteral("rateLimitReduceOnlyLimitOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_limit_order_cost"), QStringLiteral("1")));
        row.insert(QStringLiteral("rateLimitReduceOnlyMarketOrderCost"), venueExecutionValue_(venueKey, QStringLiteral("rate_limit_reduce_only_market_order_cost"), QStringLiteral("1")));
        out.push_back(std::move(row));
    }
    return out;
}

std::vector<QVariantMap> BacktestViewModel::venueExecutionRows_() const {
    return venueExecutionRowsForPaths_(orderedSessionPathsForRun_());
}

QString BacktestViewModel::selectedSymbol() const {
    const QString manual = symbolOverride_.trimmed().toUpper();
    if (!manual.isEmpty()) return manual;
    const QString fromManifest = manifestValue(selectedSessionPath(), QStringLiteral("symbols")).trimmed().toUpper();
    if (!fromManifest.isEmpty()) return fromManifest;
    return symbolFromSessionId(selectedSessionId_).toUpper();
}

QString BacktestViewModel::backtestsDirectory() const {
    const QString path = selectedSessionPath();
    return path.isEmpty() ? QString{} : QDir(path).absoluteFilePath(QStringLiteral("backtests"));
}

QVariantList BacktestViewModel::strategyChoices() const {
    QVariantList out;
    const int selectedCount = selectedSessionCount();
    const std::size_t count = hft_backtest::strategyMetadataCount();
    for (std::size_t i = 0; i < count; ++i) {
        const hft_backtest::StrategyMetadata* metadata = hft_backtest::strategyMetadataAt(i);
        if (metadata == nullptr || metadata->id == nullptr) continue;
        if (!strategyMetadataSupportsSessionCount(*metadata, selectedCount)) continue;
        QVariantMap row;
        const QString id = qString(metadata->id);
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), id);
        const int minCount = metadata->minSessionCount == 0u ? 1 : static_cast<int>(metadata->minSessionCount);
        const int maxCount = metadata->maxSessionCount == 0u ? 1 : static_cast<int>(metadata->maxSessionCount);
        row.insert(QStringLiteral("minSessions"), minCount);
        row.insert(QStringLiteral("maxSessions"), maxCount);
        row.insert(QStringLiteral("rightText"), strategySessionRangeText(*metadata));
        out.push_back(row);
    }
    return out;
}


QVariantList BacktestViewModel::indicatorProfileChoices() const {
    QVariantList out;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return out;
    for (std::size_t i = 0; i < metadata->indicatorCount && i < hft_backtest::kStrategyMetadataMaxIndicators; ++i) {
        const hft_backtest::StrategyIndicatorMetadata& indicator = metadata->indicators[i];
        if (indicator.id == nullptr || indicator.id[0] == '\0') continue;
        out.push_back(indicatorChoice(indicator));
    }
    return out;
}
QVariantList BacktestViewModel::configModeChoices() const {
    QVariantList out;
    QVariantMap fixed;
    fixed.insert(QStringLiteral("id"), QStringLiteral("fixed"));
    fixed.insert(QStringLiteral("label"), QStringLiteral("Fixed bps"));
    out.push_back(fixed);
    return out;
}

QVariantList BacktestViewModel::sweepCurveLimitChoices() const {
    return sweepCurveLimitChoiceRows();
}

QVariantList BacktestViewModel::sweepViewChoices() const {
    return sweepViewChoiceRows();
}

QVariantList BacktestViewModel::sweepMetricChoices() const {
    QVariantList out = sweepMetricChoiceRows();
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return out;
    QVariantList legs;
    if (!record->sweepCurves.empty()) legs = record->sweepCurves.front().toMap().value(QStringLiteral("legs")).toList();
    if (legs.empty() && !record->sweepRows.empty()) legs = record->sweepRows.front().toMap().value(QStringLiteral("legs")).toList();
    for (const QVariant& value : legs) {
        const QVariantMap leg = value.toMap();
        const int legIndex = leg.value(QStringLiteral("legIndex"), out.size() - 1).toInt();
        const QString id = sweepLegMetricKey(legIndex);
        bool exists = false;
        for (const QVariant& existing : out) {
            if (existing.toMap().value(QStringLiteral("id")).toString() == id) {
                exists = true;
                break;
            }
        }
        if (exists) continue;
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), leg.value(QStringLiteral("label"), QStringLiteral("Leg %1").arg(legIndex + 1)).toString());
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::strategyParameters() const {
    QVariantList out;
    const hft_backtest::StrategyMetadata* metadata = metadataForStrategy(selectedStrategy_);
    if (metadata == nullptr) return out;
    std::vector<std::uint8_t> emittedGroups;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        const std::uint8_t group = param == nullptr ? 0u : param->exclusiveGroup;
        if (group != 0u) {
            if (std::find(emittedGroups.begin(), emittedGroups.end(), group) == emittedGroups.end()) {
                emittedGroups.push_back(group);
                QVariantMap choiceRow;
                choiceRow.insert(QStringLiteral("key"), paramGroupKey(*metadata, group));
                choiceRow.insert(QStringLiteral("label"), paramGroupKey(*metadata, group));
                choiceRow.insert(QStringLiteral("value"), activeParamByGroup_.value(static_cast<int>(group)));
                choiceRow.insert(QStringLiteral("isChoice"), true);
                choiceRow.insert(QStringLiteral("group"), static_cast<int>(group));
                choiceRow.insert(QStringLiteral("choices"), paramGroupChoices(*metadata, group));
                out.push_back(choiceRow);
            }
            if (activeParamByGroup_.value(static_cast<int>(group)) != key) continue;
        }
        QVariantMap row;
        row.insert(QStringLiteral("key"), key);
        row.insert(QStringLiteral("label"), key);
        row.insert(QStringLiteral("description"), param == nullptr ? QString{} : qString(param->descriptionRu));
        row.insert(QStringLiteral("value"), paramValues_.value(key));
        row.insert(QStringLiteral("mode"), paramModes_.value(key, QStringLiteral("fixed")));
        row.insert(QStringLiteral("min"), paramMinValues_.value(key));
        row.insert(QStringLiteral("max"), paramMaxValues_.value(key));
        row.insert(QStringLiteral("step"), paramStepValues_.value(key));
        row.insert(QStringLiteral("modeChoices"), sweepParamModeChoices());
        row.insert(QStringLiteral("isChoice"), false);
        out.push_back(row);
    }
    return out;
}
QVariantList BacktestViewModel::runs() const {
    QVariantList out;
    for (const auto& record : records_) {
        QVariantMap row;
        row.insert(QStringLiteral("runId"), record.runId);
        row.insert(QStringLiteral("id"), record.runId);
        row.insert(QStringLiteral("label"), record.displayName.isEmpty() ? record.runId : record.displayName);
        row.insert(QStringLiteral("configText"), record.configText);
        row.insert(QStringLiteral("status"), record.status);
        row.insert(QStringLiteral("strategy"), record.strategy);
        row.insert(QStringLiteral("pnlText"), record.pnlText);
        row.insert(QStringLiteral("pnlPositive"), record.totalPnlE8 > 0);
        row.insert(QStringLiteral("pnlNegative"), record.totalPnlE8 < 0);
        row.insert(QStringLiteral("rightText"), record.pnlText);
        row.insert(QStringLiteral("filePath"), record.filePath);
        row.insert(QStringLiteral("fileName"), record.fileName);
        row.insert(QStringLiteral("modifiedText"), QDateTime::fromMSecsSinceEpoch(record.modifiedMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        row.insert(QStringLiteral("errorCount"), record.errorCount);
        row.insert(QStringLiteral("valid"), record.valid);
        row.insert(QStringLiteral("sweep"), record.sweep);
        row.insert(QStringLiteral("detailsLoaded"), record.detailsLoaded);
        out.push_back(row);
    }
    return out;
}

bool BacktestViewModel::canRun() const {
    return !running_ && !selectedSessionPath().trimmed().isEmpty() && !selectedStrategy_.trimmed().isEmpty() && strategySupportsSelectedSessionCount_();
}

void BacktestViewModel::startBacktest() {
    startBacktestWithOverrides_({}, QString{});
}

void BacktestViewModel::startBacktestWithOverrides_(const QHash<QString, QString>& overrides, const QString& suffix) {
    if (!canRun()) return;
    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);

    const QString outputSessionPath = selectedSessionPath();
    const QStringList sessionPaths = orderedSessionPathsForRun_();
    if (sessionPaths.empty()) {
        setRunning_(false);
        setStatusText_(QStringLiteral("Select at least one session"));
        return;
    }
    QString venueError;
    for (const QString& path : sessionPaths) {
        if (!sessionVenueIsKnown(path, &venueError)) {
            setStatusText_(venueError);
            return;
        }
    }

    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting"));
    setStatusText_(QStringLiteral("Backtest running"));

    const QString strategy = selectedStrategy_;
    QString runId = runId_();
    if (!suffix.trimmed().isEmpty()) runId += QStringLiteral("-") + cleanRunSlugPart(suffix);
    activeRunId_ = runId;
    const RunConfigWriteResult config = writeRunConfig_(runId, overrides, false);
    if (!config.ok()) {
        activeRunId_.clear();
        setRunning_(false);
        setStatusText_(QStringLiteral("Failed to write backtest config: %1").arg(config.error));
        return;
    }
    const QString configPath = config.path;
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
    const quint64 orderLatency = marketOrderLatency;
    const quint64 cancelLatency = cancelOrderLatency;
    const qint64 initialBalance = decimalE8Value_(initialBalanceUsdt_, 0);
    const bool rateLimitsEnabled = rateLimitsEnabled_;
    std::vector<std::int64_t> legInitialBalances;
    std::vector<hft_backtest::BacktestFeeSchedule> feeSchedules;
    std::vector<hft_backtest::BacktestLatencySchedule> latencySchedules;
    std::vector<hft_backtest::BacktestRateLimitSchedule> rateLimitSchedules;
    const std::vector<QVariantMap> venueRows = venueExecutionRows_();
    legInitialBalances.reserve(venueRows.size());
    feeSchedules.reserve(venueRows.size());
    latencySchedules.reserve(venueRows.size());
    rateLimitSchedules.reserve(venueRows.size());
    for (const QVariantMap& row : venueRows) {
        const QString exchange = row.value(QStringLiteral("exchange")).toString();
        const QString market = row.value(QStringLiteral("market")).toString();
        if (exchange.isEmpty() || market.isEmpty()) continue;
        legInitialBalances.push_back(decimalE8Value_(row.value(QStringLiteral("initialBalanceUsdt")).toString(), initialBalance));
        feeSchedules.push_back(feeScheduleFromVenueRow(row));
        hft_backtest::BacktestLatencySchedule latency{};
        latency.exchange = exchange.toStdString();
        latency.market = market.toStdString();
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
        latencySchedules.push_back(std::move(latency));
        hft_backtest::BacktestRateLimitSchedule rateLimit = rateLimitScheduleFromVenueRow(row);
        if (!rateLimit.buckets.empty() || !rateLimit.actions.empty()) rateLimitSchedules.push_back(std::move(rateLimit));
    }
    const QString indicatorProfile = selectedIndicatorProfile_;
    configureWorkerThreadStack_();
    worker_ = std::thread([this, outputSessionPath, sessionPaths, strategy, runId, configPath, indicatorProfile, latencySeed, marketDataLatency, marketDataJitter, marketOrderLatency, marketOrderJitter, limitOrderLatency, limitOrderJitter, cancelOrderLatency, cancelOrderJitter, userDataLatency, userDataJitter, orderLatency, cancelLatency, initialBalance, rateLimitsEnabled, legInitialBalances = std::move(legInitialBalances), feeSchedules = std::move(feeSchedules), latencySchedules = std::move(latencySchedules), rateLimitSchedules = std::move(rateLimitSchedules)] {
        try {
        hft_backtest::BacktestRunRequest request{};
        request.sessionPath = sessionPaths.front().toStdString();
        if (sessionPaths.size() > 1) {
            request.sessions.reserve(static_cast<std::size_t>(sessionPaths.size() - 1));
            for (qsizetype i = 1; i < sessionPaths.size(); ++i) {
                const QString& path = sessionPaths.at(i);
                hft_backtest::BacktestSessionRequest leg{};
                leg.path = path.toStdString();
                leg.venue = venueSectionForSession(path).toStdString();
                leg.symbol = symbolForSessionPath(path).toStdString();
                request.sessions.push_back(std::move(leg));
            }
        }
        request.configPath = configPath.toStdString();
        request.strategy = strategy.toStdString();
        request.indicatorProfile = indicatorProfile.toStdString();
        request.runId = runId.toStdString();
        request.requestId = runId.toStdString();
        request.latencySeed = latencySeed;
        request.marketDataLatency.baseUs = marketDataLatency;
        request.marketDataLatency.jitterUs = marketDataJitter;
        request.marketOrderLatency.baseUs = marketOrderLatency;
        request.marketOrderLatency.jitterUs = marketOrderJitter;
        request.limitOrderLatency.baseUs = limitOrderLatency;
        request.limitOrderLatency.jitterUs = limitOrderJitter;
        request.cancelOrderLatency.baseUs = cancelOrderLatency;
        request.cancelOrderLatency.jitterUs = cancelOrderJitter;
        request.userDataLatency.baseUs = userDataLatency;
        request.userDataLatency.jitterUs = userDataJitter;
        request.orderLatencyUs = orderLatency;
        request.cancelLatencyUs = cancelLatency;
        request.initialBalanceE8 = initialBalance;
        request.legInitialBalancesE8 = legInitialBalances;
        request.feeSchedules = feeSchedules;
        request.latencySchedules = latencySchedules;
        request.rateLimitSchedules = rateLimitSchedules;
        request.executionPipeline = guiBacktestExecutionPipeline();
        request.rateLimitsEnabled = rateLimitsEnabled;
        request.captureStrategySpread = true;
        request.outputPath = (QDir(outputSessionPath).absoluteFilePath(QStringLiteral("backtests/%1").arg(runId))).toStdString();

        const auto result = hft_backtest::runBacktest(request, progressCallback, this);
        const QString status = statusTextFor(result.status, result.error, result.warnings);
        const QString selected = QString::fromStdString(result.runId);
        QMetaObject::invokeMethod(this, [this, status, selected] {
            if (!activeRunId_.isEmpty() && !selected.isEmpty() && activeRunId_ != selected) return;
            activeRunId_.clear();
            setRunning_(false);
            setProgress_(100, status);
            setStatusText_(status);
            if (!selected.isEmpty()) selectedRunId_ = selected;
            refresh();
            reloadSessions();
        }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            const QString status = QStringLiteral("Backtest crashed: ") + QString::fromUtf8(ex.what());
            QMetaObject::invokeMethod(this, [this, status, runId] {
                if (!activeRunId_.isEmpty() && activeRunId_ != runId) return;
                activeRunId_.clear();
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        } catch (...) {
            const QString status = QStringLiteral("Backtest crashed: unknown exception");
            QMetaObject::invokeMethod(this, [this, status, runId] {
                if (!activeRunId_.isEmpty() && activeRunId_ != runId) return;
                activeRunId_.clear();
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        }
    });
}

void BacktestViewModel::startSweep() {
    if (!canRun()) return;

    std::vector<hft_backtest::BacktestSweepParamRange> ranges;
    for (const QString& key : paramOrder_) {
        const hft_backtest::StrategyParamMetadata* param = paramMetadataFor(selectedStrategy_, key);
        if (param != nullptr && param->exclusiveGroup != 0u && activeParamByGroup_.value(static_cast<int>(param->exclusiveGroup)) != key) continue;
        const QString mode = normalizedParamMode(paramModes_.value(key, QStringLiteral("fixed")));
        if (mode == QStringLiteral("fixed")) continue;
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

    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);

    const QString outputSessionPath = selectedSessionPath();
    const QStringList sessionPaths = orderedSessionPathsForRun_();
    if (sessionPaths.empty()) {
        setRunning_(false);
        setStatusText_(QStringLiteral("Select at least one session"));
        return;
    }
    QString venueError;
    for (const QString& path : sessionPaths) {
        if (!sessionVenueIsKnown(path, &venueError)) {
            setStatusText_(venueError);
            return;
        }
    }

    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting sweep"));
    setStatusText_(QStringLiteral("Sweep running"));

    const QString strategy = selectedStrategy_;
    const QString runId = QStringLiteral("sweep-") + runId_();
    const RunConfigWriteResult config = writeRunConfig_(QStringLiteral("sweeps/%1").arg(runId), {}, true);
    if (!config.ok()) {
        setRunning_(false);
        setStatusText_(QStringLiteral("Failed to write sweep config: %1").arg(config.error));
        return;
    }
    const QString configPath = config.path;
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
    std::vector<std::int64_t> legInitialBalances;
    std::vector<hft_backtest::BacktestFeeSchedule> feeSchedules;
    std::vector<hft_backtest::BacktestLatencySchedule> latencySchedules;
    std::vector<hft_backtest::BacktestRateLimitSchedule> rateLimitSchedules;
    const std::vector<QVariantMap> venueRows = venueExecutionRows_();
    legInitialBalances.reserve(venueRows.size());
    feeSchedules.reserve(venueRows.size());
    latencySchedules.reserve(venueRows.size());
    rateLimitSchedules.reserve(venueRows.size());
    for (const QVariantMap& row : venueRows) {
        const QString exchange = row.value(QStringLiteral("exchange")).toString();
        const QString market = row.value(QStringLiteral("market")).toString();
        if (exchange.isEmpty() || market.isEmpty()) continue;
        legInitialBalances.push_back(decimalE8Value_(row.value(QStringLiteral("initialBalanceUsdt")).toString(), initialBalance));
        feeSchedules.push_back(feeScheduleFromVenueRow(row));
        hft_backtest::BacktestLatencySchedule latency{};
        latency.exchange = exchange.toStdString();
        latency.market = market.toStdString();
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
        latencySchedules.push_back(std::move(latency));
        hft_backtest::BacktestRateLimitSchedule rateLimit = rateLimitScheduleFromVenueRow(row);
        if (!rateLimit.buckets.empty() || !rateLimit.actions.empty()) rateLimitSchedules.push_back(std::move(rateLimit));
    }
    const QString indicatorProfile = selectedIndicatorProfile_;

    configureWorkerThreadStack_();
    worker_ = std::thread([this, outputSessionPath, sessionPaths, strategy, runId, configPath, indicatorProfile, latencySeed, searchSeed, runBudget, marketDataLatency, marketDataJitter, marketOrderLatency, marketOrderJitter, limitOrderLatency, limitOrderJitter, cancelOrderLatency, cancelOrderJitter, userDataLatency, userDataJitter, initialBalance, rateLimitsEnabled, legInitialBalances = std::move(legInitialBalances), feeSchedules = std::move(feeSchedules), latencySchedules = std::move(latencySchedules), rateLimitSchedules = std::move(rateLimitSchedules), ranges = std::move(ranges)] {
        try {
        hft_backtest::BacktestSweepRequest request{};
        request.baseRun.sessionPath = sessionPaths.front().toStdString();
        if (sessionPaths.size() > 1) {
            request.baseRun.sessions.reserve(static_cast<std::size_t>(sessionPaths.size() - 1));
            for (qsizetype i = 1; i < sessionPaths.size(); ++i) {
                const QString& path = sessionPaths.at(i);
                hft_backtest::BacktestSessionRequest leg{};
                leg.path = path.toStdString();
                leg.venue = venueSectionForSession(path).toStdString();
                leg.symbol = symbolForSessionPath(path).toStdString();
                request.baseRun.sessions.push_back(std::move(leg));
            }
        }
        request.baseRun.configPath = configPath.toStdString();
        request.baseRun.strategy = strategy.toStdString();
        request.baseRun.indicatorProfile = indicatorProfile.toStdString();
        request.baseRun.latencySeed = latencySeed;
        request.baseRun.marketDataLatency.baseUs = marketDataLatency;
        request.baseRun.marketDataLatency.jitterUs = marketDataJitter;
        request.baseRun.marketOrderLatency.baseUs = marketOrderLatency;
        request.baseRun.marketOrderLatency.jitterUs = marketOrderJitter;
        request.baseRun.limitOrderLatency.baseUs = limitOrderLatency;
        request.baseRun.limitOrderLatency.jitterUs = limitOrderJitter;
        request.baseRun.cancelOrderLatency.baseUs = cancelOrderLatency;
        request.baseRun.cancelOrderLatency.jitterUs = cancelOrderJitter;
        request.baseRun.userDataLatency.baseUs = userDataLatency;
        request.baseRun.userDataLatency.jitterUs = userDataJitter;
        request.baseRun.orderLatencyUs = marketOrderLatency;
        request.baseRun.cancelLatencyUs = cancelOrderLatency;
        request.baseRun.initialBalanceE8 = initialBalance;
        request.baseRun.legInitialBalancesE8 = legInitialBalances;
        request.baseRun.feeSchedules = feeSchedules;
        request.baseRun.latencySchedules = latencySchedules;
        request.baseRun.rateLimitSchedules = rateLimitSchedules;
        request.baseRun.rateLimitsEnabled = rateLimitsEnabled;
        request.baseRun.writeArtifacts = false;
        request.sweepId = runId.toStdString();
        request.runBudget = runBudget;
        request.searchSeed = searchSeed;
        request.outputPath = (QDir(outputSessionPath).absoluteFilePath(QStringLiteral("backtests/sweeps/%1").arg(runId))).toStdString();
        request.ranges = ranges;

        const auto result = hft_backtest::runBacktestSweep(request, progressCallback, this);
        const QString status = sweepStatusTextFor(result.status, result.error);
        const QString selected = QString::fromStdString(result.sweepId);
        QMetaObject::invokeMethod(this, [this, status, selected] {
            setRunning_(false);
            setProgress_(100, status);
            setStatusText_(status);
            if (!selected.isEmpty()) selectedRunId_ = selected;
            refresh();
            reloadSessions();
        }, Qt::QueuedConnection);
        } catch (const std::exception& ex) {
            const QString status = QStringLiteral("Sweep crashed: ") + QString::fromUtf8(ex.what());
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        } catch (...) {
            const QString status = QStringLiteral("Sweep crashed: unknown exception");
            QMetaObject::invokeMethod(this, [this, status] {
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
                refresh();
            }, Qt::QueuedConnection);
        }
    });
}

void BacktestViewModel::applySweepPoint(int rowIndex) {
    const QVariantList rows = selectedSweepRows();
    if (rowIndex < 0 || rowIndex >= rows.size()) return;
    const QVariantMap params = rows.at(rowIndex).toMap().value(QStringLiteral("params")).toMap();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString key = it.key().trimmed().toLower();
        if (!paramOrder_.contains(key)) continue;
        paramValues_.insert(key, QString::number(it.value().toLongLong()));
        paramModes_.insert(key, QStringLiteral("fixed"));
    }
    savePersistentConfig_();
    emit strategyParametersChanged();
}

void BacktestViewModel::applySweepPointById(int pointId) {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep) return;
    for (const QVariant& value : record->sweepCurves) {
        const QVariantMap curve = value.toMap();
        if (curve.value(QStringLiteral("pointId")).toInt() != pointId) continue;
        const QVariantMap params = curve.value(QStringLiteral("params")).toMap();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            const QString key = it.key().trimmed().toLower();
            if (!paramOrder_.contains(key)) continue;
            paramValues_.insert(key, QString::number(it.value().toLongLong()));
            paramModes_.insert(key, QStringLiteral("fixed"));
        }
        savePersistentConfig_();
        emit strategyParametersChanged();
        return;
    }
}

void BacktestViewModel::startDetailedRunFromSweepPoint(int rowIndex) {
    const QVariantList rows = selectedSweepRows();
    if (rowIndex < 0 || rowIndex >= rows.size()) return;
    const QVariantMap params = rows.at(rowIndex).toMap().value(QStringLiteral("params")).toMap();
    QHash<QString, QString> overrides;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) overrides.insert(it.key().trimmed().toLower(), QString::number(it.value().toLongLong()));
    startBacktestWithOverrides_(overrides, QStringLiteral("detail"));
}

void BacktestViewModel::startDetailedRunFromSweepPointById(int pointId) {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep) return;
    for (const QVariant& value : record->sweepCurves) {
        const QVariantMap curve = value.toMap();
        if (curve.value(QStringLiteral("pointId")).toInt() != pointId) continue;
        const QVariantMap params = curve.value(QStringLiteral("params")).toMap();
        QHash<QString, QString> overrides;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) overrides.insert(it.key().trimmed().toLower(), QString::number(it.value().toLongLong()));
        startBacktestWithOverrides_(overrides, QStringLiteral("detail-p%1").arg(pointId));
        return;
    }
}
void BacktestViewModel::cancelBacktest() {
    cancelRequested_.store(true, std::memory_order_release);
    if (running_) setStatusText_(QStringLiteral("Cancelling backtest"));
}

void BacktestViewModel::applyWorkerProgress(int percent, const QString& text) {
    if (!running_ && percent < 100) return;
    setProgress_(percent, text);
}

void BacktestViewModel::stopWorker_() {
    if (worker_.joinable()) worker_.join();
}

}  // namespace hftrec::gui
