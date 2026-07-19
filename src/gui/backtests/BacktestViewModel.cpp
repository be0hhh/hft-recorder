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
#include <optional>
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
#include "gui/models/RecordingCatalog.hpp"

namespace hftrec::gui {
namespace {

QString statusTextFor(hft_backtest::Status status, const std::string& error = {}, const std::vector<std::string>& warnings = {}) {
    if (status == hft_backtest::Status::Ok) {
        if (!warnings.empty()) {
            const int count = static_cast<int>(warnings.size());
            return QStringLiteral("Backtest complete: %1 warning%2")
                .arg(count)
                .arg(count == 1 ? QString{} : QStringLiteral("s"));
        }
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

QStringList preparedSessionPaths(const std::vector<BacktestPreparedSession>& sessions) {
    QStringList paths;
    paths.reserve(static_cast<qsizetype>(sessions.size()));
    for (const BacktestPreparedSession& session : sessions) paths.push_back(session.path);
    return paths;
}

std::vector<hft_backtest::BacktestSessionRequest> preparedSecondarySessions(
    const std::vector<BacktestPreparedSession>& sessions) {
    std::vector<hft_backtest::BacktestSessionRequest> out;
    if (sessions.size() <= 1u) return out;
    out.reserve(sessions.size() - 1u);
    for (std::size_t i = 1; i < sessions.size(); ++i) {
        hft_backtest::BacktestSessionRequest request;
        request.path = sessions[i].path.toStdString();
        request.venue = sessions[i].venue.toStdString();
        request.symbol = sessions[i].symbol.toStdString();
        out.push_back(std::move(request));
    }
    return out;
}

QString catalogSessionSummary(const hftrec::recordings::RecordedSessionInfo& session, const BacktestLegCounts& counts) {
    QString summary = sessionBacktestSummaryText(static_cast<int>(session.bookTickerCount), counts, session.startedAtNs);
    if (session.candleCount > 0) summary += QStringLiteral(" | C %1").arg(session.candleCount);
    return appendSessionHealthSummary(summary,
                                      QString::fromStdString(session.sessionHealth),
                                      QString::fromStdString(session.warningSummary));
}

QString sessionDataSummaryText(std::uint64_t bookTickerCount, std::uint64_t tradesCount) {
    return QStringLiteral("BTK %1 | TRD %2")
        .arg(QString::number(static_cast<qulonglong>(bookTickerCount)),
             QString::number(static_cast<qulonglong>(tradesCount)));
}

QVariantMap tradeModeChoice(const QString& id, const QString& label) {
    QVariantMap row;
    row.insert(QStringLiteral("id"), id);
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), id);
    return row;
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
    setStatusText_(QStringLiteral("Loading sessions"));
}

BacktestViewModel::~BacktestViewModel() {
    if (asyncLoadContext_) asyncLoadContext_->alive.store(false, std::memory_order_release);
    ++previewLoadGeneration_;
    ++detailsLoadGeneration_;
    stopAsyncLoaders_();
    cancelBacktest();
    stopWorker_();
    settings_.sync();
}

QString BacktestViewModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

QVariantList BacktestViewModel::sessions() const {
    return sessions_;
}

QObject* BacktestViewModel::recordingCatalog() const {
    return recordingCatalog_;
}

void BacktestViewModel::setRecordingCatalog(QObject* recordingCatalog) {
    auto* typedCatalog = qobject_cast<RecordingCatalog*>(recordingCatalog);
    if (typedCatalog == recordingCatalog_) return;
    recordingCatalog_ = typedCatalog;
    reconnectRecordingCatalog_();
    applyLoadedSessions_(++sessionsLoadGeneration_, loadSessions_());
    emit recordingCatalogChanged();
}

QVariantList BacktestViewModel::loadSessions_() const {
    QVariantList out;
    if (recordingCatalog_ == nullptr || !recordingCatalog_->hasSnapshot()) return out;
    const auto& snapshot = recordingCatalog_->snapshot();
    for (const auto& group : snapshot.discovery.groups) {
        QVariantList groupPaths;
        int groupFirstLegBacktests = 0;
        int groupSecondLegBacktests = 0;
        int groupBacktests = 0;
        for (const auto& session : group.sessions) {
            const QString sessionId = QString::fromStdString(session.sessionId);
            groupPaths.push_back(QString::fromStdString(session.path.string()));
            const BacktestLegCounts counts = snapshot.backtestCountsBySession.value(sessionId);
            groupFirstLegBacktests += counts.firstLeg;
            groupSecondLegBacktests += counts.secondLeg;
            groupBacktests += counts.total > 0 ? counts.total : counts.firstLeg + counts.secondLeg;
        }
        if (!group.sessions.empty()) {
            QVariantMap groupRow;
            groupRow.insert(QStringLiteral("id"), QStringLiteral("group:%1").arg(QString::fromStdString(group.id)));
            groupRow.insert(QStringLiteral("label"), QString::fromStdString(group.title));
            groupRow.insert(QStringLiteral("path"), QString::fromStdString(group.sessions.front().path.string()));
            groupRow.insert(QStringLiteral("sessionPaths"), groupPaths);
            groupRow.insert(QStringLiteral("hasManifest"), true);
            groupRow.insert(QStringLiteral("hasBacktests"), groupFirstLegBacktests > 0 || groupSecondLegBacktests > 0);
            groupRow.insert(QStringLiteral("backtestCount"), groupBacktests);
            groupRow.insert(QStringLiteral("firstLegBacktestCount"), groupFirstLegBacktests);
            groupRow.insert(QStringLiteral("secondLegBacktestCount"), groupSecondLegBacktests);
            groupRow.insert(QStringLiteral("isGroup"), true);
            groupRow.insert(QStringLiteral("selectable"), false);
            groupRow.insert(QStringLiteral("groupId"), QString::fromStdString(group.id));
            groupRow.insert(QStringLiteral("rightText"), QStringLiteral("%1 legs | rows %2 | BT %3")
                                                    .arg(group.sessions.size())
                                                    .arg(QString::number(static_cast<qulonglong>(group.totalRows)))
                                                    .arg(groupBacktests));
            out.push_back(groupRow);
        }

        for (const auto& session : group.sessions) {
            const QString id = QString::fromStdString(session.sessionId);
            const QString path = QString::fromStdString(session.path.string());
            const QString exchange = QString::fromStdString(session.exchange).trimmed().toLower();
            const QString rawMarket = QString::fromStdString(session.market).trimmed().toLower();
            const QString market = normalizedFeeMarket(rawMarket);
            const QString symbol = QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front()).trimmed().toUpper();
            const BacktestLegCounts backtestCounts = snapshot.backtestCountsBySession.value(id);
            const int backtestCount = backtestCounts.total > 0 ? backtestCounts.total : backtestCounts.firstLeg + backtestCounts.secondLeg;
            QVariantMap row;
            row.insert(QStringLiteral("id"), id);
            row.insert(QStringLiteral("label"), QStringLiteral("%1/%2 %3")
                                            .arg(exchange, rawMarket, symbol));
            row.insert(QStringLiteral("path"), path);
            row.insert(QStringLiteral("sessionPaths"), QVariantList{path});
            row.insert(QStringLiteral("exchange"), exchange);
            row.insert(QStringLiteral("market"), market);
            row.insert(QStringLiteral("symbol"), symbol);
            row.insert(QStringLiteral("venue"), venueSectionFor(exchange, market));
            row.insert(QStringLiteral("tradeCount"), static_cast<qulonglong>(session.tradesCount));
            row.insert(QStringLiteral("bookTickerCount"), static_cast<qulonglong>(session.bookTickerCount));
            row.insert(QStringLiteral("dataSummary"), sessionDataSummaryText(session.bookTickerCount, session.tradesCount));
            row.insert(QStringLiteral("hasManifest"), true);
            row.insert(QStringLiteral("hasBacktests"), backtestCount > 0);
            row.insert(QStringLiteral("backtestCount"), backtestCount);
            row.insert(QStringLiteral("firstLegBacktestCount"), backtestCounts.firstLeg);
            row.insert(QStringLiteral("secondLegBacktestCount"), backtestCounts.secondLeg);
            row.insert(QStringLiteral("isGroup"), false);
            row.insert(QStringLiteral("selectable"), true);
            row.insert(QStringLiteral("parentGroupId"), QString::fromStdString(group.id));
            row.insert(QStringLiteral("rightText"), catalogSessionSummary(session, backtestCounts));
            out.push_back(row);
        }
    }
    return out;
}

void BacktestViewModel::reconnectRecordingCatalog_() {
    if (catalogSnapshotConnection_) disconnect(catalogSnapshotConnection_);
    if (recordingCatalog_ == nullptr) return;
    catalogSnapshotConnection_ = connect(recordingCatalog_, &RecordingCatalog::snapshotChanged, this, [this]() {
        applyLoadedSessions_(++sessionsLoadGeneration_, loadSessions_());
    });
}

void BacktestViewModel::reloadSessionsAsync_() {
    applyLoadedSessions_(++sessionsLoadGeneration_, loadSessions_());
}

void BacktestViewModel::applyLoadedSessions_(std::uint64_t generation, QVariantList sessions) {
    if (generation != sessionsLoadGeneration_) return;
    sessions_ = std::move(sessions);
    const bool manualSessionActive = !manualSessionPath_.trimmed().isEmpty();
    if (!manualSessionActive) {
        const QVariantMap selectedRow = sessionRowById(sessions_, selectedSessionId_);
        const bool selectedGroup = selectedRow.value(QStringLiteral("isGroup")).toBool();
        if (selectedSessionId_.trimmed().isEmpty() || selectedRow.isEmpty() || (!selectedGroup && !sessionRowSelectable(selectedRow))) {
            selectedSessionId_ = firstSelectableSessionId(sessions_);
        }
    }
    loadLegSelectionForCurrentSession_();
    const bool primaryChanged = normalizeSelectedPrimaryLeg_();
    if (primaryChanged) savePersistentConfig_();
    const bool strategyChanged = ensureSelectedStrategySupportsSessionCount_();
    emit sessionsChanged();
    emit selectedSessionChanged();
    emit multiSessionChanged();
    emit primaryLegChanged();
    if (!strategyChanged) emit selectedStrategyChanged();
    emit canRunChanged();
    emit legSelectionChanged();
    if (!manualSessionActive) {
        const std::uint64_t scheduledGeneration = generation;
        QTimer::singleShot(1000, this, [this, scheduledGeneration]() {
            if (scheduledGeneration == sessionsLoadGeneration_) scheduleRefresh_();
        });
    }
}

QString BacktestViewModel::selectedSessionPath() const {
    if (!manualSessionPath_.trimmed().isEmpty()) return manualSessionPath_;
    if (selectedSessionId_.trimmed().isEmpty()) return {};
    const QVariantMap row = sessionRowById(sessions_, selectedSessionId_);
    const QStringList paths = sessionPathsFromRow(row);
    if (!paths.empty()) return paths.front();
    return sessionPathFromToken(recordingsRoot(), selectedSessionId_);
}

QStringList BacktestViewModel::candidatePathsForSessionId_(const QString& sessionId) const {
    QStringList out;
    const auto appendPath = [&out](const QString& value) {
        const QString path = normalizedPath_(value);
        if (!path.isEmpty() && !out.contains(path)) out.push_back(path);
    };

    const QString id = sessionId.trimmed();
    if (id.isEmpty()) return out;
    const QStringList rowPaths = sessionPathsFromRow(sessionRowById(sessions_, id));
    for (const QString& path : rowPaths) {
        appendPath(path);
    }
    if (out.empty()) {
        appendPath(sessionPathFromToken(recordingsRoot(), id));
    }
    return out;
}

QStringList BacktestViewModel::selectedSessionCandidatePaths_() const {
    QStringList out;
    const auto appendPath = [&out](const QString& value) {
        const QString path = normalizedPath_(value);
        if (!path.isEmpty() && !out.contains(path)) out.push_back(path);
    };

    bool selectedSessionOwnsAllLegs = false;
    if (!manualSessionPath_.trimmed().isEmpty()) {
        appendPath(manualSessionPath_);
    } else {
        const QVariantMap selectedRow = sessionRowById(sessions_, selectedSessionId_);
        const QStringList primaryPaths = sessionPathsFromRow(selectedRow);
        selectedSessionOwnsAllLegs = selectedRow.value(QStringLiteral("isGroup")).toBool() || primaryPaths.size() > 1;
        for (const QString& path : primaryPaths) {
            appendPath(path);
        }
        if (out.empty()) {
            const QString primary = selectedSessionPath();
            appendPath(primary);
        }
    }
    if (selectedSessionOwnsAllLegs) return out;
    const QStringList tokens = extraSessionIds_.split(QRegularExpression(QStringLiteral("[,;\\n]+")), Qt::SkipEmptyParts);
    const QString root = recordingsRoot();
    for (const QString& token : tokens) {
        const QStringList rowPaths = sessionPathsFromRow(sessionRowById(sessions_, token.trimmed()));
        const QStringList paths = rowPaths.empty() ? QStringList{sessionPathFromToken(root, token)} : rowPaths;
        for (const QString& path : paths) {
            appendPath(path);
        }
    }
    return out;
}

QStringList BacktestViewModel::legSelectionCandidatePaths_() const {
    const QString pendingSessionId = pendingLegSelectionSessionId_.trimmed();
    return pendingSessionId.isEmpty() ? selectedSessionCandidatePaths_() : candidatePathsForSessionId_(pendingSessionId);
}

QStringList BacktestViewModel::selectedSessionPaths_() const {
    QStringList out;
    const QStringList candidates = selectedSessionCandidatePaths_();
    for (const QString& path : candidates) {
        if (!disabledSessionLegPaths_.contains(path)) out.push_back(path);
    }
    return out;
}

QStringList BacktestViewModel::orderedSessionPathsForRun_() const {
    QStringList paths = selectedSessionPaths_();
    if (selectedStrategy_ != QStringLiteral("basis_convergence_probe") || paths.size() < 2) return paths;

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
            if (futuresIndex < 0) futuresIndex = i;
        }
    }
    if (spotIndex < 0 || futuresIndex < 0 || spotIndex == futuresIndex) return paths;
    return QStringList{paths.at(spotIndex), paths.at(futuresIndex)};
}

int BacktestViewModel::normalizedSelectedPrimaryLegIndexForPaths_(const QStringList& paths, const QStringList& disabledPaths) const {
    if (paths.empty()) return 0;
    if (selectedPrimaryLegIndex_ >= 0 && selectedPrimaryLegIndex_ < paths.size() &&
        !disabledPaths.contains(paths.at(selectedPrimaryLegIndex_))) {
        return selectedPrimaryLegIndex_;
    }
    for (int i = 0; i < paths.size(); ++i) {
        if (!disabledPaths.contains(paths.at(i))) return i;
    }
    return 0;
}

int BacktestViewModel::normalizedSelectedPrimaryLegIndex_() const {
    return normalizedSelectedPrimaryLegIndexForPaths_(selectedSessionCandidatePaths_(), disabledSessionLegPaths_);
}

int BacktestViewModel::selectedPrimaryLegIndexForPaths_(const QStringList& paths) const {
    if (paths.empty()) return 0;
    const QStringList candidates = selectedSessionCandidatePaths_();
    const int candidateIndex = normalizedSelectedPrimaryLegIndex_();
    if (candidateIndex >= 0 && candidateIndex < candidates.size()) {
        const int runIndex = paths.indexOf(candidates.at(candidateIndex));
        if (runIndex >= 0) return runIndex;
    }
    return 0;
}

bool BacktestViewModel::normalizeSelectedPrimaryLeg_() {
    const int normalized = normalizedSelectedPrimaryLegIndex_();
    if (selectedPrimaryLegIndex_ == normalized) return false;
    selectedPrimaryLegIndex_ = normalized;
    return true;
}

QVariantList BacktestViewModel::selectedSessionLegs() const {
    return sessionLegRowsForPaths_(selectedSessionCandidatePaths_(), disabledSessionLegPaths_);
}

QVariantMap BacktestViewModel::sessionCatalogRowForPath_(const QString& path) const {
    const QString normalized = normalizedPath_(path);
    if (normalized.isEmpty()) return {};
    for (const QVariant& value : sessions_) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("isGroup")).toBool()) continue;
        if (normalizedPath_(row.value(QStringLiteral("path")).toString()) == normalized) return row;
    }
    return {};
}

QString BacktestViewModel::sessionExchangeForPath_(const QString& path) const {
    const QVariantMap row = sessionCatalogRowForPath_(path);
    const QString exchange = row.value(QStringLiteral("exchange")).toString().trimmed().toLower();
    return exchange.isEmpty() ? manifestValue(path, QStringLiteral("exchange")).trimmed().toLower() : exchange;
}

QString BacktestViewModel::sessionMarketForPath_(const QString& path) const {
    const QVariantMap row = sessionCatalogRowForPath_(path);
    const QString market = row.value(QStringLiteral("market")).toString().trimmed().toLower();
    return market.isEmpty() ? normalizedFeeMarket(manifestValue(path, QStringLiteral("market"))) : market;
}

QString BacktestViewModel::sessionSymbolForPath_(const QString& path) const {
    const QVariantMap row = sessionCatalogRowForPath_(path);
    const QString symbol = row.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
    return symbol.isEmpty() ? symbolForSessionPath(path) : symbol;
}

QString BacktestViewModel::sessionVenueSectionForPath_(const QString& path) const {
    const QVariantMap row = sessionCatalogRowForPath_(path);
    const QString venue = row.value(QStringLiteral("venue")).toString().trimmed();
    if (!venue.isEmpty()) return venue;
    QString exchange = row.value(QStringLiteral("exchange")).toString().trimmed().toLower();
    QString market = row.value(QStringLiteral("market")).toString().trimmed().toLower();
    if (exchange.isEmpty() || market.isEmpty()) {
        const SessionManifestSnapshot manifest = loadSessionManifestSnapshot(path);
        if (exchange.isEmpty()) {
            exchange = manifestValue(manifest, QStringLiteral("exchange")).trimmed().toLower();
        }
        if (market.isEmpty()) {
            market = manifestValue(manifest, QStringLiteral("market")).trimmed().toLower();
        }
    }
    return venueSectionFor(exchange, market);
}

std::uint64_t BacktestViewModel::sessionBookTickerCountForPath_(const QString& path) const {
    const QVariantMap row = sessionCatalogRowForPath_(path);
    const QVariant value = row.value(QStringLiteral("bookTickerCount"));
    if (value.isValid()) return value.toULongLong();
    return manifestChannelDeclaredCount(path, QStringLiteral("bookticker"));
}

std::uint64_t BacktestViewModel::sessionTradeCountForPath_(const QString& path) const {
    const QVariantMap row = sessionCatalogRowForPath_(path);
    const QVariant value = row.value(QStringLiteral("tradeCount"));
    if (value.isValid()) return value.toULongLong();
    return manifestChannelDeclaredCount(path, QStringLiteral("trades"));
}

QString BacktestViewModel::venueExecutionKeyForPath_(const QString& path) const {
    const QVariantMap row = sessionCatalogRowForPath_(path);
    QString exchange = row.value(QStringLiteral("exchange")).toString().trimmed().toLower();
    QString market = row.value(QStringLiteral("market")).toString().trimmed().toLower();
    if (exchange.isEmpty() || market.isEmpty()) {
        const SessionManifestSnapshot manifest = loadSessionManifestSnapshot(path);
        if (exchange.isEmpty()) {
            exchange = manifestValue(manifest, QStringLiteral("exchange")).trimmed().toLower();
        }
        if (market.isEmpty()) {
            market = manifestValue(manifest, QStringLiteral("market")).trimmed().toLower();
        }
    }
    if (exchange.isEmpty() || market.isEmpty()) return {};
    return exchange + QLatin1Char('|') + normalizedFeeMarket(market);
}

QVariantList BacktestViewModel::sessionLegRowsForPaths_(const QStringList& paths, const QStringList& disabledPaths) const {
    QVariantList out;
    const int primaryIndex = normalizedSelectedPrimaryLegIndexForPaths_(paths, disabledPaths);
    const bool primaryOnly = selectedTradeMode_ == QStringLiteral("primary");
    for (int i = 0; i < paths.size(); ++i) {
        QVariantMap row;
        const QString path = paths.at(i);
        const bool enabled = !disabledPaths.contains(path);
        const bool primary = i == primaryIndex;
        const QVariantMap catalogRow = sessionCatalogRowForPath_(path);
        const bool needsManifest =
            catalogRow.value(QStringLiteral("exchange")).toString().trimmed().isEmpty() ||
            catalogRow.value(QStringLiteral("market")).toString().trimmed().isEmpty() ||
            catalogRow.value(QStringLiteral("symbol")).toString().trimmed().isEmpty() ||
            !catalogRow.value(QStringLiteral("bookTickerCount")).isValid() ||
            !catalogRow.value(QStringLiteral("tradeCount")).isValid();
        std::optional<SessionManifestSnapshot> manifest;
        if (needsManifest) manifest.emplace(loadSessionManifestSnapshot(path));
        const QString exchangeFromManifest = manifest.has_value()
            ? manifestValue(*manifest, QStringLiteral("exchange")).trimmed().toLower()
            : QString{};
        const QString marketFromManifest = manifest.has_value()
            ? manifestValue(*manifest, QStringLiteral("market"))
            : QString{};
        const QString exchange = catalogRow.value(QStringLiteral("exchange")).toString().trimmed().toLower().isEmpty()
            ? exchangeFromManifest
            : catalogRow.value(QStringLiteral("exchange")).toString().trimmed().toLower();
        const QString catalogMarket = catalogRow.value(QStringLiteral("market")).toString().trimmed().toLower();
        const QString market = catalogMarket.isEmpty()
            ? normalizedFeeMarket(marketFromManifest)
            : catalogMarket;
        const QString catalogSymbol = catalogRow.value(QStringLiteral("symbol")).toString().trimmed().toUpper();
        const QString symbol = catalogSymbol.isEmpty() && manifest.has_value()
            ? symbolForSessionPath(*manifest)
            : catalogSymbol;
        const QString catalogVenue = catalogRow.value(QStringLiteral("venue")).toString().trimmed();
        const QString venue = catalogVenue.isEmpty()
            ? venueSectionFor(exchange, market)
            : catalogVenue;
        const QString venueKey = exchange.isEmpty() || market.isEmpty()
            ? QString{}
            : exchange + QLatin1Char('|') + normalizedFeeMarket(market);
        const QVariant bookTickerValue = catalogRow.value(QStringLiteral("bookTickerCount"));
        const QVariant tradesValue = catalogRow.value(QStringLiteral("tradeCount"));
        const std::uint64_t bookTickerCount = bookTickerValue.isValid()
            ? bookTickerValue.toULongLong()
            : (manifest.has_value()
                   ? manifestChannelDeclaredCount(*manifest, QStringLiteral("bookticker"))
                   : 0u);
        const std::uint64_t tradesCount = tradesValue.isValid()
            ? tradesValue.toULongLong()
            : (manifest.has_value()
                   ? manifestChannelDeclaredCount(*manifest, QStringLiteral("trades"))
                   : 0u);
        const QString makerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("maker_fee_bps"));
        const QString takerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("taker_fee_bps"));
        row.insert(QStringLiteral("index"), i);
        row.insert(QStringLiteral("enabled"), enabled);
        row.insert(QStringLiteral("primary"), primary);
        row.insert(QStringLiteral("tradable"), enabled && (!primaryOnly || primary));
        row.insert(QStringLiteral("tradeMode"), selectedTradeMode_);
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("id"), sessionIdFromPath_(path));
        row.insert(QStringLiteral("symbol"), symbol);
        row.insert(QStringLiteral("venue"), venue);
        row.insert(QStringLiteral("venueKey"), venueKey);
        row.insert(QStringLiteral("exchange"), exchange);
        row.insert(QStringLiteral("market"), market);
        row.insert(QStringLiteral("bookTickerCount"), static_cast<qulonglong>(bookTickerCount));
        row.insert(QStringLiteral("tradeCount"), static_cast<qulonglong>(tradesCount));
        row.insert(QStringLiteral("dataSummary"), sessionDataSummaryText(bookTickerCount, tradesCount));
        row.insert(QStringLiteral("initialBalanceUsdt"), venueExecutionValue_(venueKey, QStringLiteral("initial_balance_usdt"), initialBalanceUsdt_));
        if (!makerFeeOverride.isEmpty()) row.insert(QStringLiteral("makerFeeBps"), makerFeeOverride);
        if (!takerFeeOverride.isEmpty()) row.insert(QStringLiteral("takerFeeBps"), takerFeeOverride);
        row.insert(QStringLiteral("executionPresetSummary"),
                   exchangeExecutionPresetSummary(exchange,
                                                  market,
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
            .arg(venue, symbol));
        out.push_back(row);
    }
    return out;
}

int BacktestViewModel::selectedPrimaryLegIndex() const {
    return normalizedSelectedPrimaryLegIndex_();
}

QVariantList BacktestViewModel::tradeModeChoices() const {
    return QVariantList{
        tradeModeChoice(QStringLiteral("all"), QStringLiteral("All")),
        tradeModeChoice(QStringLiteral("primary"), QStringLiteral("Primary")),
    };
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

QVariantMap BacktestViewModel::venueExecutionRow_(const QString& exchange,
                                                  const QString& market) const {
    const QString venueKey = exchange.trimmed().toLower() + QLatin1Char('|') +
        normalizedFeeMarket(market);
    const QString makerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("maker_fee_bps"));
    const QString takerFeeOverride = venueExecutionOverrideValue_(venueKey, QStringLiteral("taker_fee_bps"));
    QVariantMap row;
    row.insert(QStringLiteral("exchange"), exchange.trimmed().toLower());
    row.insert(QStringLiteral("market"), normalizedFeeMarket(market));
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
    return row;
}

std::vector<QVariantMap> BacktestViewModel::venueExecutionRowsForPaths_(const QStringList& paths) const {
    std::vector<QVariantMap> out;
    QSet<QString> emitted;
    out.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) {
        const QVariantMap catalogRow = sessionCatalogRowForPath_(path);
        QString exchange = catalogRow.value(QStringLiteral("exchange")).toString().trimmed().toLower();
        QString market = catalogRow.value(QStringLiteral("market")).toString().trimmed().toLower();
        if (exchange.isEmpty() || market.isEmpty()) {
            const SessionManifestSnapshot manifest = loadSessionManifestSnapshot(path);
            if (exchange.isEmpty()) {
                exchange = manifestValue(manifest, QStringLiteral("exchange")).trimmed().toLower();
            }
            if (market.isEmpty()) {
                market = normalizedFeeMarket(manifestValue(manifest, QStringLiteral("market")));
            }
        }
        const QString venueKey = exchange.isEmpty() || market.isEmpty()
            ? QString{}
            : exchange + QLatin1Char('|') + normalizedFeeMarket(market);
        if (venueKey.isEmpty() || emitted.contains(venueKey)) continue;
        emitted.insert(venueKey);
        out.push_back(venueExecutionRow_(exchange, market));
    }
    return out;
}

BacktestPreparedSessions BacktestViewModel::prepareSelectedSessions_() const {
    BacktestPreparedSessions prepared;
    const QStringList paths = selectedSessionPaths_();
    if (paths.empty()) {
        prepared.error = QStringLiteral("Select at least one session");
        return prepared;
    }

    std::vector<BacktestPreparedSession> candidates;
    candidates.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths) {
        const SessionManifestSnapshot manifest = loadSessionManifestSnapshot(path);
        if (!manifest.ready()) {
            prepared.error = QStringLiteral("%1: %2").arg(manifest.error(), path);
            return prepared;
        }
        BacktestPreparedSession session;
        session.path = path;
        session.exchange = manifestValue(manifest, QStringLiteral("exchange")).trimmed().toLower();
        session.market = manifestValue(manifest, QStringLiteral("market")).trimmed().toLower();
        session.venue = venueSectionFor(session.exchange, session.market);
        session.symbol = symbolForSessionPath(manifest);
        session.configSymbol = session.symbol;
        if (path == selectedSessionPath()) {
            const QString manualSymbol = symbolOverride_.trimmed().toUpper();
            const QString manifestSymbol =
                manifestValue(manifest, QStringLiteral("symbols")).trimmed().toUpper();
            session.configSymbol = !manualSymbol.isEmpty()
                ? manualSymbol
                : (!manifestSymbol.isEmpty()
                       ? manifestSymbol
                       : symbolFromSessionId(selectedSessionId_).toUpper());
        }
        if (session.exchange.isEmpty() || session.market.isEmpty() || session.venue.isEmpty()) {
            prepared.error = QStringLiteral("Unsupported venue: exchange=%1 market=%2 in session=%3")
                                 .arg(session.exchange.isEmpty() ? QStringLiteral("<empty>") : session.exchange,
                                      session.market.isEmpty() ? QStringLiteral("<empty>") : session.market,
                                      path);
            return prepared;
        }
        if (session.symbol.isEmpty() || session.configSymbol.isEmpty()) {
            prepared.error = QStringLiteral("missing symbol for session: %1").arg(path);
            return prepared;
        }
        candidates.push_back(std::move(session));
    }

    if (selectedStrategy_ != QStringLiteral("basis_convergence_probe") || candidates.size() < 2u) {
        prepared.sessions = std::move(candidates);
        return prepared;
    }
    const auto isFuturesMarket = [](const QString& market) {
        return market == QStringLiteral("futures") ||
               market == QStringLiteral("future") ||
               market == QStringLiteral("forts") ||
               market == QStringLiteral("usdt") ||
               market == QStringLiteral("usdc") ||
               market == QStringLiteral("linear");
    };
    int spotIndex = -1;
    int futuresIndex = -1;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].market == QStringLiteral("spot")) {
            spotIndex = static_cast<int>(i);
        } else if (futuresIndex < 0 && isFuturesMarket(candidates[i].market)) {
            futuresIndex = static_cast<int>(i);
        }
    }
    if (spotIndex >= 0 && futuresIndex >= 0 && spotIndex != futuresIndex) {
        prepared.sessions.push_back(std::move(candidates[static_cast<std::size_t>(spotIndex)]));
        prepared.sessions.push_back(std::move(candidates[static_cast<std::size_t>(futuresIndex)]));
    } else {
        prepared.sessions = std::move(candidates);
    }
    return prepared;
}

BacktestExecutionPolicy BacktestViewModel::executionPolicyForSessions_(
    const std::vector<BacktestPreparedSession>& sessions,
    bool includeExecutionLatency) const {
    const quint64 pingLatency = latencyValue_(pingLatencyUs_, 1000);
    const quint64 marketDataLatency = latencyValue_(marketDataLatencyUs_, 0);
    const quint64 marketDataJitter = latencyValue_(marketDataJitterUs_, 0);
    const quint64 marketOrderLatency = latencyValue_(marketOrderLatencyUs_, pingLatency);
    const quint64 marketOrderJitter = latencyValue_(marketOrderJitterUs_, 0);
    const quint64 limitOrderLatency = latencyValue_(limitOrderLatencyUs_, pingLatency);
    const quint64 limitOrderJitter = latencyValue_(limitOrderJitterUs_, 0);
    const quint64 cancelOrderLatency = latencyValue_(cancelOrderLatencyUs_, limitOrderLatency);
    const quint64 cancelOrderJitter = latencyValue_(cancelOrderJitterUs_, limitOrderJitter);
    const quint64 userDataLatency = latencyValue_(userDataLatencyUs_, 0);
    const quint64 userDataJitter = latencyValue_(userDataJitterUs_, 0);

    BacktestExecutionPolicy policy{};
    policy.latencySeed = latencyValue_(latencySeed_, 0);
    policy.marketDataLatency = {marketDataLatency, marketDataJitter};
    policy.marketOrderLatency = {marketOrderLatency, marketOrderJitter};
    policy.limitOrderLatency = {limitOrderLatency, limitOrderJitter};
    policy.cancelOrderLatency = {cancelOrderLatency, cancelOrderJitter};
    policy.userDataLatency = {userDataLatency, userDataJitter};
    policy.orderLatencyUs = marketOrderLatency;
    policy.cancelLatencyUs = cancelOrderLatency;
    policy.initialBalanceE8 = decimalE8Value_(initialBalanceUsdt_, 0);
    policy.rateLimitsEnabled = rateLimitsEnabled_;
    policy.strictRateLimitsEnabled = strictRateLimitsEnabled_;

    std::vector<QVariantMap> venueRows;
    QSet<QString> emittedVenueKeys;
    venueRows.reserve(sessions.size());
    for (const BacktestPreparedSession& session : sessions) {
        const QString venueKey = session.exchange + QLatin1Char('|') +
            normalizedFeeMarket(session.market);
        if (venueKey.isEmpty() || emittedVenueKeys.contains(venueKey)) continue;
        emittedVenueKeys.insert(venueKey);
        venueRows.push_back(venueExecutionRow_(session.exchange, session.market));
    }
    policy.legInitialBalancesE8.reserve(venueRows.size());
    policy.feeSchedules.reserve(venueRows.size());
    policy.latencySchedules.reserve(venueRows.size());
    policy.rateLimitSchedules.reserve(venueRows.size());
    for (const QVariantMap& row : venueRows) {
        const QString exchange = row.value(QStringLiteral("exchange")).toString();
        const QString market = row.value(QStringLiteral("market")).toString();
        if (exchange.isEmpty() || market.isEmpty()) continue;
        policy.legInitialBalancesE8.push_back(
            decimalE8Value_(row.value(QStringLiteral("initialBalanceUsdt")).toString(),
                            policy.initialBalanceE8));
        policy.feeSchedules.push_back(feeScheduleFromVenueRow(row));
        if (usePerVenueLatencySchedules(includeExecutionLatency)) {
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
            policy.latencySchedules.push_back(std::move(latency));
        }
        hft_backtest::BacktestRateLimitSchedule rateLimit = rateLimitScheduleFromVenueRow(row);
        if (!rateLimit.buckets.empty() || !rateLimit.actions.empty()) {
            policy.rateLimitSchedules.push_back(std::move(rateLimit));
        }
    }
    return policy;
}

QString BacktestViewModel::selectedSymbol() const {
    const QString manual = symbolOverride_.trimmed().toUpper();
    if (!manual.isEmpty()) return manual;
    const QString fromManifest = manifestValue(selectedSessionPath(), QStringLiteral("symbols")).trimmed().toUpper();
    if (!fromManifest.isEmpty()) return fromManifest;
    return symbolFromSessionId(selectedSessionId_).toUpper();
}

QString BacktestViewModel::backtestsDirectory() const {
    const QStringList paths = orderedSessionPathsForRun_();
    const QString path = paths.empty() ? selectedSessionPath() : paths.front();
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
        QString category = QStringLiteral("single");
        if (maxCount > 2) category = QStringLiteral("multi-leg");
        else if (minCount == 2 && maxCount == 2) category = QStringLiteral("2-leg");
        else if (maxCount == 2) category = QStringLiteral("1-2 leg");
        row.insert(QStringLiteral("minSessions"), minCount);
        row.insert(QStringLiteral("maxSessions"), maxCount);
        row.insert(QStringLiteral("category"), category);
        row.insert(QStringLiteral("rightText"), QStringLiteral("%1 | %2").arg(category, strategySessionRangeText(*metadata)));
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
    fixed.insert(QStringLiteral("label"), QStringLiteral("Fixed"));
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
    return !running_ && !selectedSessionPaths_().empty() &&
           !selectedStrategy_.trimmed().isEmpty() && strategySupportsSelectedSessionCount_();
}

void BacktestViewModel::startBacktest() {
    startBacktestWithOverrides_({}, QString{});
}

void BacktestViewModel::startBacktestWithOverrides_(const QHash<QString, QString>& overrides, const QString& suffix) {
    if (!canRun()) return;
    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);

    const BacktestPreparedSessions prepared = prepareSelectedSessions_();
    if (!prepared.ready()) {
        setRunning_(false);
        setStatusText_(prepared.error);
        return;
    }
    const QStringList sessionPaths = preparedSessionPaths(prepared.sessions);
    const std::vector<hft_backtest::BacktestSessionRequest> secondarySessions =
        preparedSecondarySessions(prepared.sessions);
    const QString outputSessionPath = sessionPaths.front();

    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting"));
    setStatusText_(QStringLiteral("Backtest running"));

    const QString strategy = selectedStrategy_;
    QString runId = runIdForSymbol_(prepared.sessions.front().configSymbol);
    if (!suffix.trimmed().isEmpty()) runId += QStringLiteral("-") + cleanRunSlugPart(suffix);
    activeRunId_ = runId;
    const RunConfigWriteResult config =
        writeRunConfigForPreparedSessions_(runId, prepared.sessions, overrides, false);
    if (!config.ok()) {
        activeRunId_.clear();
        setRunning_(false);
        setStatusText_(QStringLiteral("Failed to write backtest config: %1").arg(config.error));
        return;
    }
    const QString configPath = config.path;
    const BacktestExecutionPolicy executionPolicy =
        executionPolicyForSessions_(prepared.sessions, false);
    const QString indicatorProfile = selectedIndicatorProfile_;
    const int primaryLegIndex = selectedPrimaryLegIndexForPaths_(sessionPaths);
    const QString tradeMode = selectedTradeMode_;
    configureWorkerThreadStack_();
    worker_ = std::thread([this, outputSessionPath, sessionPaths, secondarySessions, strategy, runId, configPath, indicatorProfile, primaryLegIndex, tradeMode, executionPolicy] {
        try {
        hft_backtest::BacktestRunRequest request{};
        request.sessionPath = sessionPaths.front().toStdString();
        request.sessions = secondarySessions;
        request.configPath = configPath.toStdString();
        request.strategy = strategy.toStdString();
        request.indicatorProfile = indicatorProfile.toStdString();
        request.hasPrimaryLegIndex = true;
        request.primaryLegIndex = static_cast<std::uint32_t>(primaryLegIndex);
        request.tradeMode = tradeMode == QStringLiteral("primary")
            ? hft_backtest::BacktestTradeMode::PrimaryOnly
            : hft_backtest::BacktestTradeMode::AllLegs;
        request.runId = runId.toStdString();
        request.requestId = runId.toStdString();
        applyBacktestExecutionPolicy(request, executionPolicy);
        request.captureStrategySpread = false;
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
    startSweep_(false);
}

void BacktestViewModel::startExecutionLatencySweep() {
    startSweep_(true);
}

void BacktestViewModel::startSweep_(bool includeExecutionLatency) {
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
    if (includeExecutionLatency) {
        const auto appendLatencyRange = [&ranges](const char* key, quint64 configured) {
            hft_backtest::BacktestSweepParamRange range{};
            range.key = key;
            range.minRaw = 0;
            const quint64 signedMax = static_cast<quint64>(std::numeric_limits<std::int64_t>::max());
            const quint64 doubled = configured == 0u
                ? 2000u
                : (configured > signedMax / 2u ? signedMax : configured * 2u);
            range.maxRaw = static_cast<std::int64_t>(doubled);
            range.stepRaw = std::max<std::int64_t>(1, range.maxRaw / 4);
            range.mode = hft_backtest::BacktestSweepParamMode::Grid;
            ranges.push_back(std::move(range));
        };
        const quint64 ping = latencyValue_(pingLatencyUs_, 1000);
        appendLatencyRange("@market_order_latency_us", latencyValue_(marketOrderLatencyUs_, ping));
        appendLatencyRange("@limit_order_latency_us", latencyValue_(limitOrderLatencyUs_, ping));
        appendLatencyRange("@user_data_latency_us", latencyValue_(userDataLatencyUs_, 0));
    }
    if (ranges.empty()) {
        setStatusText_(QStringLiteral("Choose at least one Sweep parameter or enable execution latency sweep"));
        return;
    }

    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);

    const BacktestPreparedSessions prepared = prepareSelectedSessions_();
    if (!prepared.ready()) {
        setRunning_(false);
        setStatusText_(prepared.error);
        return;
    }
    const QStringList sessionPaths = preparedSessionPaths(prepared.sessions);
    const std::vector<hft_backtest::BacktestSessionRequest> secondarySessions =
        preparedSecondarySessions(prepared.sessions);
    const QString outputSessionPath = sessionPaths.front();

    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting sweep"));
    setStatusText_(QStringLiteral("Sweep running"));

    const QString strategy = selectedStrategy_;
    const QString runId = QStringLiteral("sweep-") +
        runIdForSymbol_(prepared.sessions.front().configSymbol);
    const RunConfigWriteResult config = writeRunConfigForPreparedSessions_(
        QStringLiteral("sweeps/%1").arg(runId), prepared.sessions, {}, true);
    if (!config.ok()) {
        setRunning_(false);
        setStatusText_(QStringLiteral("Failed to write sweep config: %1").arg(config.error));
        return;
    }
    const QString configPath = config.path;
    const quint64 searchSeed = latencyValue_(sweepSeed_, 0);
    const quint64 runBudget = latencyValue_(sweepBudget_, 64);
    const BacktestExecutionPolicy executionPolicy =
        executionPolicyForSessions_(prepared.sessions, includeExecutionLatency);
    const QString indicatorProfile = selectedIndicatorProfile_;
    const int primaryLegIndex = selectedPrimaryLegIndexForPaths_(sessionPaths);
    const QString tradeMode = selectedTradeMode_;

    configureWorkerThreadStack_();
    worker_ = std::thread([this, outputSessionPath, sessionPaths, secondarySessions, strategy, runId, configPath, indicatorProfile, primaryLegIndex, tradeMode, searchSeed, runBudget, executionPolicy, ranges = std::move(ranges)] {
        try {
        hft_backtest::BacktestSweepRequest request{};
        request.baseRun.sessionPath = sessionPaths.front().toStdString();
        request.baseRun.sessions = secondarySessions;
        request.baseRun.configPath = configPath.toStdString();
        request.baseRun.strategy = strategy.toStdString();
        request.baseRun.indicatorProfile = indicatorProfile.toStdString();
        request.baseRun.hasPrimaryLegIndex = true;
        request.baseRun.primaryLegIndex = static_cast<std::uint32_t>(primaryLegIndex);
        request.baseRun.tradeMode = tradeMode == QStringLiteral("primary")
            ? hft_backtest::BacktestTradeMode::PrimaryOnly
            : hft_backtest::BacktestTradeMode::AllLegs;
        applyBacktestExecutionPolicy(request.baseRun, executionPolicy);
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
