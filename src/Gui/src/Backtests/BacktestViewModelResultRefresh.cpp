#include "BacktestViewModel.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <utility>
#include <vector>

#include "BacktestSessionHelpers.hpp"
#include "BacktestSessionSummary.hpp"
#include "BacktestStrategyConfigHelpers.hpp"

namespace hftrec::gui {
namespace {

void appendUniquePath(QStringList& paths, const QString& path) {
    const QString trimmed = path.trimmed();
    if (!trimmed.isEmpty() && !paths.contains(trimmed)) paths.push_back(trimmed);
}

}  // namespace

void BacktestViewModel::refresh() {
    deferredLegSelectionRefresh_ = false;
    refreshTimer_.stop();

    if (resultsLoading_) {
        pendingResultsRefresh_ = true;
        setResultsLoading_(true, QStringLiteral("Backtest result refresh queued"));
        return;
    }

    ++previewLoadGeneration_;
    setPreviewLoading_(false);
    if (!pendingDetailsRunId_.isEmpty()) {
        pendingDetailsRunId_.clear();
        setDetailsLoading_(false);
    }
    updateWatcher_();

    ResultRefreshRequest request;
    request.dirPath = backtestsDirectory();
    request.selectedStrategy = selectedStrategy_;
    const QStringList selectedPaths = selectedSessionPaths_();
    for (const QString& path : selectedPaths) request.selectedSessionIds.push_back(sessionIdFromPath_(path));
    request.selectionKey = resultRefreshSelectionKey_(request.dirPath, request.selectedSessionIds, request.selectedStrategy);
    request.cachedRecords = records_;

    const std::uint64_t generation = ++resultsLoadGeneration_;
    setResultsLoading_(true, request.dirPath.isEmpty()
        ? QStringLiteral("No backtest directory selected")
        : QStringLiteral("Loading backtest results"));
    if (!running_) setStatusText_(resultsLoadingText_);

    auto context = asyncLoadContext_;
    BacktestViewModel* target = this;
    asyncLoaders_.emplace_back([context, target, generation, request = std::move(request)]() mutable {
        ResultRefreshSnapshot snapshot;
        std::vector<RunRecord> next;

        const auto cachedForPath = [&request](const QString& filePath) -> const RunRecord* {
            const QString targetPath = QFileInfo(filePath).absoluteFilePath();
            const auto it = std::find_if(request.cachedRecords.begin(), request.cachedRecords.end(), [&targetPath](const RunRecord& record) {
                return record.filePath == targetPath;
            });
            return it == request.cachedRecords.end() ? nullptr : &(*it);
        };

        const auto addRunDir = [&request, &snapshot, &next, &cachedForPath](const QFileInfo& runDir) {
            const QDir candidateDir(runDir.absoluteFilePath());
            const QString manifestPath = candidateDir.absoluteFilePath(QStringLiteral("manifest.json"));
            if (!QFileInfo::exists(manifestPath)) return;
            if (!backtestManifestMatchesLegs(manifestPath, request.selectedSessionIds)) return;

            const RunRecord* cached = cachedForPath(runDir.absoluteFilePath());
            RunRecord record = loadRecord_(runDir.absoluteFilePath(), RecordLoadMode::MetadataOnly);
            if (!record.strategy.isEmpty() && record.strategy != request.selectedStrategy) return;

            const bool metadataMatches = cached != nullptr
                && cached->manifestPath == manifestPath
                && fileStampMatches_(manifestPath, cached->manifestModifiedMs, cached->manifestSize);
            const bool equityMatches = metadataMatches
                && record.equityPath == cached->equityPath
                && fileStampMatches_(cached->equityPath, cached->equityModifiedMs, cached->equitySize);
            if (equityMatches) {
                record.equityPoints = cached->equityPoints;
                record.resultScopes = cached->resultScopes;
                record.scopedEquityPoints = cached->scopedEquityPoints;
                record.scopedResultMetrics = cached->scopedResultMetrics;
                record.scopedInitialBalanceE8 = cached->scopedInitialBalanceE8;
                record.scopedPnlMinE8 = cached->scopedPnlMinE8;
                record.scopedPnlMaxE8 = cached->scopedPnlMaxE8;
                record.pnlMinE8 = cached->pnlMinE8;
                record.pnlMaxE8 = cached->pnlMaxE8;
            }

            const bool sweepMatches = metadataMatches
                && cached->detailsLoaded
                && record.sweepRowsPath == cached->sweepRowsPath
                && record.sweepCurvesPath == cached->sweepCurvesPath
                && fileStampMatches_(cached->sweepRowsPath, cached->sweepRowsModifiedMs, cached->sweepRowsSize)
                && fileStampMatches_(cached->sweepCurvesPath, cached->sweepCurvesModifiedMs, cached->sweepCurvesSize);
            if (cached != nullptr && cached->detailsLoaded && ((!record.sweep && equityMatches) || (record.sweep && sweepMatches))) {
                record.sweepRows = cached->sweepRows;
                record.sweepCurves = cached->sweepCurves;
                record.sweepParamKeys = cached->sweepParamKeys;
                record.detailsErrorText = cached->detailsErrorText;
                record.warningText = cached->warningText;
                record.warningCount = cached->warningCount;
                if (!cached->sweepRows.empty() || !cached->sweepCurves.empty()) {
                    record.initialBalanceE8 = cached->initialBalanceE8;
                    record.totalPnlE8 = cached->totalPnlE8;
                    record.pnlText = cached->pnlText;
                }
                record.detailsLoaded = true;
            }

            appendUniquePath(snapshot.filesToWatch, record.manifestPath);
            next.push_back(std::move(record));
        };

        if (!request.dirPath.isEmpty()) {
            QDir dir(request.dirPath);
            const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Name);
            const QDir sweepsDir(dir.absoluteFilePath(QStringLiteral("sweeps")));
            const QFileInfoList sweepDirs = sweepsDir.exists()
                ? sweepsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Name)
                : QFileInfoList{};
            next.reserve(static_cast<std::size_t>(dirs.size() + sweepDirs.size()));
            for (const QFileInfo& runDir : dirs) {
                if (runDir.fileName() == QStringLiteral("sweeps")) continue;
                addRunDir(runDir);
            }
            for (const QFileInfo& sweepDir : sweepDirs) addRunDir(sweepDir);
        }

        std::sort(next.begin(), next.end(), [](const RunRecord& lhs, const RunRecord& rhs) {
            if (lhs.modifiedMs != rhs.modifiedMs) return lhs.modifiedMs > rhs.modifiedMs;
            return lhs.fileName < rhs.fileName;
        });
        snapshot.records = std::move(next);

        if (!context || !context->alive.load(std::memory_order_acquire)) return;
        QMetaObject::invokeMethod(target, [context, target, generation, selectionKey = request.selectionKey, snapshot = std::move(snapshot)]() mutable {
            if (!context || !context->alive.load(std::memory_order_acquire)) return;
            target->applyLoadedResults_(generation, selectionKey, std::move(snapshot));
        }, Qt::QueuedConnection);
    });
}

void BacktestViewModel::applyLoadedResults_(std::uint64_t generation, const QString& selectionKey, ResultRefreshSnapshot snapshot) {
    const bool staleGeneration = generation != resultsLoadGeneration_;
    const bool staleSelection = selectionKey != currentResultSelectionKey_();
    if (staleGeneration || staleSelection) {
        const bool refreshAgain = pendingResultsRefresh_ || staleSelection;
        pendingResultsRefresh_ = false;
        setResultsLoading_(false);
        if (refreshAgain) scheduleRefresh_();
        return;
    }

    const bool refreshAgain = pendingResultsRefresh_;
    pendingResultsRefresh_ = false;
    records_ = std::move(snapshot.records);
    setResultsLoading_(false);

    if (!selectedRunId_.isEmpty() && selectedRecord_() == nullptr) selectedRunId_.clear();
    if (selectedRunId_.isEmpty() && !records_.empty()) selectedRunId_ = records_.front().runId;
    if (running_ && !activeRunId_.isEmpty()) {
        const auto active = std::find_if(records_.begin(), records_.end(), [this](const RunRecord& record) {
            return record.runId == activeRunId_;
        });
        if (active != records_.end()) {
            const QString terminalStatus = active->status.trimmed().toLower();
            if (terminalStatus == QStringLiteral("complete") ||
                terminalStatus == QStringLiteral("error") ||
                terminalStatus == QStringLiteral("failed") ||
                terminalStatus == QStringLiteral("cancelled") ||
                terminalStatus == QStringLiteral("canceled")) {
                const QString status = active->sweep
                    ? (terminalStatus == QStringLiteral("complete") ? QStringLiteral("Sweep complete") : QStringLiteral("Sweep ") + terminalStatus)
                    : (terminalStatus == QStringLiteral("complete") ? QStringLiteral("Backtest complete") : QStringLiteral("Backtest ") + terminalStatus);
                const QString statusWithWarnings = terminalStatus == QStringLiteral("complete") && active->warningCount > 0
                    ? status + QStringLiteral(": %1 warning%2").arg(active->warningCount).arg(active->warningCount == 1 ? QString{} : QStringLiteral("s"))
                    : status;
                activeRunId_.clear();
                setRunning_(false);
                setProgress_(100, statusWithWarnings);
                setStatusText_(statusWithWarnings);
            }
        }
    }

    if (const RunRecord* selected = selectedRecord_()) {
        appendUniquePath(snapshot.filesToWatch, selected->equityPath);
        if (selected->detailsLoaded) {
            appendUniquePath(snapshot.filesToWatch, selected->sweepRowsPath);
            appendUniquePath(snapshot.filesToWatch, selected->sweepCurvesPath);
        }
    }

    if (!running_) {
        if (selectedSessionPath().isEmpty()) {
            setStatusText_(QStringLiteral("Select a session and strategy"));
        } else if (selectedSessionPaths_().empty()) {
            setStatusText_(QStringLiteral("Select at least one leg"));
        } else if (!strategySupportsSelectedSessionCount_()) {
            const QString gateText = strategySessionGateText(selectedStrategy_, selectedSessionCount());
            setStatusText_(gateText.isEmpty() ? QStringLiteral("Selected strategy does not support selected sessions") : gateText);
        } else {
            setStatusText_(QStringLiteral("Watching %1 result%2")
                               .arg(static_cast<qulonglong>(records_.size()))
                               .arg(records_.size() == 1u ? QString{} : QStringLiteral("s")));
        }
    }

    const QStringList watchedFiles = watcher_.files();
    if (!watchedFiles.empty()) {
        QStringList filesToRemove;
        for (const QString& file : watchedFiles) {
            if (!snapshot.filesToWatch.contains(file)) filesToRemove.push_back(file);
        }
        if (!filesToRemove.empty()) (void)watcher_.removePaths(filesToRemove);
    }
    if (!snapshot.filesToWatch.empty()) {
        const QStringList currentFiles = watcher_.files();
        QStringList filesToAdd;
        for (const QString& file : snapshot.filesToWatch) {
            if (!QFileInfo::exists(file) || currentFiles.contains(file)) continue;
            filesToAdd.push_back(file);
        }
        if (!filesToAdd.empty()) (void)watcher_.addPaths(filesToAdd);
    }

    emit runsChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    ensureSelectedPreviewLoaded_();
    if (refreshAgain) scheduleRefresh_();
}

void BacktestViewModel::setResultsLoading_(bool loading, const QString& text) {
    const QString nextText = loading ? text : QString{};
    if (resultsLoading_ == loading && resultsLoadingText_ == nextText) return;
    resultsLoading_ = loading;
    resultsLoadingText_ = nextText;
    emit resultsLoadingChanged();
}

QString BacktestViewModel::currentResultSelectionKey_() const {
    QStringList selectedSessionIds;
    const QStringList selectedPaths = selectedSessionPaths_();
    for (const QString& path : selectedPaths) selectedSessionIds.push_back(sessionIdFromPath_(path));
    return resultRefreshSelectionKey_(backtestsDirectory(), selectedSessionIds, selectedStrategy_);
}

QString BacktestViewModel::resultRefreshSelectionKey_(const QString& dirPath, const QStringList& selectedSessionIds, const QString& selectedStrategy) {
    QStringList parts;
    parts.push_back(QDir::cleanPath(dirPath));
    parts.push_back(selectedStrategy.trimmed());
    parts.append(selectedSessionIds);
    return parts.join(QLatin1Char('\n'));
}

}  // namespace hftrec::gui
