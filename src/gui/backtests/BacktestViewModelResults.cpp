#include "gui/backtests/BacktestViewModel.hpp"

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
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "hft_backtest/backtest.hpp"
#include "hft_backtest/backtest_sweep.hpp"
#include "gui/backtests/BacktestExecutionConfigHelpers.hpp"
#include "gui/backtests/BacktestResultHelpers.hpp"
#include "gui/backtests/BacktestSessionHelpers.hpp"
#include "gui/backtests/BacktestStrategyConfigHelpers.hpp"
#include "gui/backtests/BacktestSweepHelpers.hpp"

namespace hftrec::gui {

QString BacktestViewModel::selectedJson() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->rawJson;
}

QString BacktestViewModel::selectedSummaryJson() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->summaryJson;
}

QString BacktestViewModel::selectedConfigText() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->configText;
}

QString BacktestViewModel::selectedErrorText() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->errorText;
}

QVariantList BacktestViewModel::selectedEquityPoints() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return {};
    const QString scope = effectiveResultScopeId_(*record);
    if (record->scopedEquityPoints.contains(scope)) return record->scopedEquityPoints.value(scope);
    return record->equityPoints;
}

QVariantList BacktestViewModel::resultScopeChoices() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QVariantList{} : record->resultScopes;
}

QString BacktestViewModel::selectedResultScope() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QStringLiteral("portfolio") : effectiveResultScopeId_(*record);
}

QVariantList BacktestViewModel::selectedResultMetrics() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return {};
    const QString scope = effectiveResultScopeId_(*record);
    if (record->scopedResultMetrics.contains(scope)) return record->scopedResultMetrics.value(scope);
    return record->resultMetrics;
}

QString BacktestViewModel::selectedResultMetricKey() const {
    const auto* record = selectedRecord_();
    const QVariantList metrics = selectedResultMetrics();
    if (record == nullptr || metrics.empty()) return selectedResultMetricKey_;
    for (const QVariant& value : metrics) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("key")).toString() == selectedResultMetricKey_) return selectedResultMetricKey_;
    }
    return metrics.front().toMap().value(QStringLiteral("key")).toString();
}

QVariantList BacktestViewModel::selectedResultMetricSeries() const {
    const auto* record = selectedRecord_();
    const QVariantList points = selectedEquityPoints();
    if (record == nullptr || !record->detailsLoaded || points.size() < 2) return {};

    const QString metricKey = selectedResultMetricKey();
    const QString ratioKey = selectedResultMetricRatioKey_;
    QVariantList out;
    qint64 peakPnl = std::numeric_limits<qint64>::min();
    qint64 bestPnl = std::numeric_limits<qint64>::min();
    qint64 worstPnl = std::numeric_limits<qint64>::max();

    auto valueFor = [](const QString& key, const QVariantMap& point, qint64 drawdown, qint64 mfe, qint64 mae, bool& ok) -> qint64 {
        ok = true;
        if (key == QStringLiteral("net_realized_pnl_e8") || key == QStringLiteral("realized_pnl_e8")) return point.value(QStringLiteral("realizedPnlE8")).toLongLong();
        if (key == QStringLiteral("total_pnl_e8")) return point.value(QStringLiteral("totalPnlE8")).toLongLong();
        if (key == QStringLiteral("fees_paid_e8")) return point.value(QStringLiteral("feesPaidE8")).toLongLong();
        if (key == QStringLiteral("wallet_balance_e8")) return point.value(QStringLiteral("walletBalanceE8")).toLongLong();
        if (key == QStringLiteral("max_drawdown_e8")) return drawdown;
        if (key == QStringLiteral("mfe_e8")) return mfe;
        if (key == QStringLiteral("mae_e8")) return mae;
        if (key == QStringLiteral("fills")) return point.value(QStringLiteral("fillCount")).toLongLong();
        ok = false;
        return 0;
    };

    for (const QVariant& value : points) {
        const QVariantMap point = value.toMap();
        const QVariant totalPnlValue = point.value(QStringLiteral("totalPnlE8"));
        const qint64 pnl = totalPnlValue.isValid()
                                ? totalPnlValue.toLongLong()
                                : point.value(QStringLiteral("netTotalPnlE8")).toLongLong();
        peakPnl = peakPnl == std::numeric_limits<qint64>::min() ? pnl : std::max(peakPnl, pnl);
        bestPnl = bestPnl == std::numeric_limits<qint64>::min() ? pnl : std::max(bestPnl, pnl);
        worstPnl = worstPnl == std::numeric_limits<qint64>::max() ? pnl : std::min(worstPnl, pnl);
        const qint64 drawdown = peakPnl - pnl;

        bool hasValue = false;
        const qint64 raw = valueFor(metricKey, point, drawdown, bestPnl, worstPnl, hasValue);
        if (!hasValue) return {};

        QVariantMap row;
        row.insert(QStringLiteral("index"), out.size());
        row.insert(QStringLiteral("tsNs"), point.value(QStringLiteral("tsNs")));
        row.insert(QStringLiteral("valueRaw"), raw);
        row.insert(QStringLiteral("valueText"), isE8Key(metricKey) ? e8DisplayString(raw) : QString::number(raw));
        if (!ratioKey.isEmpty() && ratioKey != metricKey) {
            bool hasDenominator = false;
            const qint64 denominator = valueFor(ratioKey, point, drawdown, bestPnl, worstPnl, hasDenominator);
            if (hasDenominator && denominator != 0) {
                row.insert(QStringLiteral("denominatorRaw"), denominator);
                row.insert(QStringLiteral("hasRatio"), true);
            }
        }
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::resultMetricRatioChoices() const {
    QVariantList out;
    QVariantMap none;
    none.insert(QStringLiteral("id"), QString{});
    none.insert(QStringLiteral("label"), QStringLiteral("None"));
    out.push_back(none);
    const QVariantList metrics = selectedResultMetrics();
    for (const QVariant& value : metrics) {
        const QVariantMap metric = value.toMap();
        if (!metric.value(QStringLiteral("hasSeries")).toBool()) continue;
        QVariantMap row;
        row.insert(QStringLiteral("id"), metric.value(QStringLiteral("key")).toString());
        row.insert(QStringLiteral("label"), metric.value(QStringLiteral("label")).toString());
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::selectedSweepRows() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    QVariantList out;
    out.reserve(record->sweepRows.size());
    const QString metricKey = selectedSweepMetric_.isEmpty() ? QStringLiteral("total_pnl_e8") : selectedSweepMetric_;
    for (const QVariant& value : record->sweepRows) out.push_back(sweepMapForMetric(value.toMap(), metricKey));
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("metricRaw")).toLongLong() > rhs.toMap().value(QStringLiteral("metricRaw")).toLongLong();
    });
    return out;
}

QVariantList BacktestViewModel::selectedSweepDistributionParamChoices() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    QStringList keys = record->sweepParamKeys;
    appendSweepParamKeysFromRows(record->sweepRows, keys);
    QVariantList out;
    for (const QString& key : keys) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), key);
        row.insert(QStringLiteral("label"), key);
        out.push_back(row);
    }
    return out;
}

QString BacktestViewModel::selectedSweepDistributionParam() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    for (const QString& key : record->sweepParamKeys) {
        if (key == selectedSweepDistributionParam_) return key;
    }
    return record->sweepParamKeys.empty() ? QString{} : record->sweepParamKeys.front();
}

QVariantList BacktestViewModel::selectedSweepCurves() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    QVariantList out;
    out.reserve(record->sweepCurves.size());
    const QString metricKey = selectedSweepMetric_.isEmpty() ? QStringLiteral("total_pnl_e8") : selectedSweepMetric_;
    for (const QVariant& value : record->sweepCurves) out.push_back(sweepMapForMetric(value.toMap(), metricKey));
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("metricRaw")).toLongLong() > rhs.toMap().value(QStringLiteral("metricRaw")).toLongLong();
    });
    const int limit = selectedSweepCurveLimit_ == QStringLiteral("all") ? out.size() : selectedSweepCurveLimit_.toInt();
    while (out.size() > limit) out.removeLast();
    return out;
}

QVariantList BacktestViewModel::selectedSweepDistributionBars() const {
    const auto* record = selectedRecord_();
    if (record == nullptr || !record->sweep || !record->detailsLoaded) return {};
    const QString paramKey = selectedSweepDistributionParam();
    if (paramKey.isEmpty()) return {};
    const QString metricKey = selectedSweepMetric_.isEmpty() ? QStringLiteral("total_pnl_e8") : selectedSweepMetric_;
    QVariantList out;
    const QVariantList rows = selectedSweepRows();
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        const QVariantMap params = row.value(QStringLiteral("params")).toMap();
        if (!params.contains(paramKey)) continue;
        QVariantMap bar = row;
        const qint64 metricRaw = row.value(QStringLiteral("metricRaw")).toLongLong();
        const qint64 paramRaw = params.value(paramKey).toLongLong();
        bar.insert(QStringLiteral("paramKey"), paramKey);
        bar.insert(QStringLiteral("paramRaw"), paramRaw);
        bar.insert(QStringLiteral("paramText"), QString::number(paramRaw));
        bar.insert(QStringLiteral("metricKey"), metricKey);
        bar.insert(QStringLiteral("metricRaw"), metricRaw);
        bar.insert(QStringLiteral("metricText"), e8DisplayString(metricRaw));
        out.push_back(bar);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        const QVariantMap left = lhs.toMap();
        const QVariantMap right = rhs.toMap();
        const qint64 leftParam = left.value(QStringLiteral("paramRaw")).toLongLong();
        const qint64 rightParam = right.value(QStringLiteral("paramRaw")).toLongLong();
        if (leftParam != rightParam) return leftParam < rightParam;
        const qint64 leftMetric = left.value(QStringLiteral("metricRaw")).toLongLong();
        const qint64 rightMetric = right.value(QStringLiteral("metricRaw")).toLongLong();
        if (leftMetric != rightMetric) return leftMetric > rightMetric;
        return left.value(QStringLiteral("pointId")).toLongLong() < right.value(QStringLiteral("pointId")).toLongLong();
    });
    return out;
}

bool BacktestViewModel::selectedIsSweep() const {
    const auto* record = selectedRecord_();
    return record != nullptr && record->sweep;
}

bool BacktestViewModel::hasEquityPoints() const {
    return !selectedEquityPoints().empty();
}

bool BacktestViewModel::selectedPreviewLoading() const {
    return previewLoading_ && !selectedRunId_.isEmpty() && selectedRunId_ == previewLoadingRunId_;
}

bool BacktestViewModel::selectedDetailsLoaded() const {
    const auto* record = selectedRecord_();
    return record != nullptr && record->detailsLoaded;
}

bool BacktestViewModel::selectedDetailsLoading() const {
    return detailsLoading_ && !selectedRunId_.isEmpty() && selectedRunId_ == detailsLoadingRunId_;
}

qint64 BacktestViewModel::selectedInitialBalanceE8() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return 0;
    const QString scope = effectiveResultScopeId_(*record);
    return record->scopedInitialBalanceE8.value(scope, record->initialBalanceE8);
}

qint64 BacktestViewModel::selectedPnlMinE8() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return 0;
    const QString scope = effectiveResultScopeId_(*record);
    return record->scopedPnlMinE8.value(scope, record->pnlMinE8);
}

qint64 BacktestViewModel::selectedPnlMaxE8() const {
    const auto* record = selectedRecord_();
    if (record == nullptr) return 0;
    const QString scope = effectiveResultScopeId_(*record);
    return record->scopedPnlMaxE8.value(scope, record->pnlMaxE8);
}

void BacktestViewModel::refresh() {
    refreshTimer_.stop();
    ++previewLoadGeneration_;
    setPreviewLoading_(false);
    if (!pendingDetailsRunId_.isEmpty()) {
        pendingDetailsRunId_.clear();
        setDetailsLoading_(false);
    }
    updateWatcher_();
    std::vector<RunRecord> next;
    const QString dirPath = backtestsDirectory();
    QStringList filesToWatch;
    if (!dirPath.isEmpty()) {
        const QStringList selectedPaths = selectedSessionPaths_();
        const QString primarySessionId = selectedPaths.empty() ? QString{} : sessionIdFromPath_(selectedPaths.at(0));
        const QString secondarySessionId = selectedPaths.size() > 1 ? sessionIdFromPath_(selectedPaths.at(1)) : QString{};
        auto addRunDir = [&](const QFileInfo& runDir) {
            const QDir candidateDir(runDir.absoluteFilePath());
            const QString manifestPath = candidateDir.absoluteFilePath(QStringLiteral("manifest.json"));
            if (!QFileInfo::exists(manifestPath)) return;
            if (!backtestManifestMatchesLegs(manifestPath, primarySessionId, secondarySessionId)) return;
            const RunRecord* cached = recordForPath_(runDir.absoluteFilePath());
            RunRecord record = loadRecord_(runDir.absoluteFilePath(), RecordLoadMode::MetadataOnly);
            if (!record.strategy.isEmpty() && record.strategy != selectedStrategy_) return;
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
                if (!cached->sweepRows.empty() || !cached->sweepCurves.empty()) {
                    record.initialBalanceE8 = cached->initialBalanceE8;
                    record.totalPnlE8 = cached->totalPnlE8;
                    record.pnlText = cached->pnlText;
                }
                record.detailsLoaded = true;
            }
            if (!record.manifestPath.isEmpty()) filesToWatch.push_back(record.manifestPath);
            next.push_back(std::move(record));
        };
        QDir dir(dirPath);
        const QFileInfoList dirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Name);
        const QDir sweepsDir(dir.absoluteFilePath(QStringLiteral("sweeps")));
        const QFileInfoList sweepDirs = sweepsDir.exists() ? sweepsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Name) : QFileInfoList{};
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
    records_ = std::move(next);

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
                activeRunId_.clear();
                setRunning_(false);
                setProgress_(100, status);
                setStatusText_(status);
            }
        }
    }
    if (const RunRecord* selected = selectedRecord_()) {
        if (!selected->equityPath.isEmpty()) filesToWatch.push_back(selected->equityPath);
        if (selected->detailsLoaded) {
            if (!selected->sweepRowsPath.isEmpty()) filesToWatch.push_back(selected->sweepRowsPath);
            if (!selected->sweepCurvesPath.isEmpty()) filesToWatch.push_back(selected->sweepCurvesPath);
        }
    }

    if (!running_) {
        if (selectedSessionPath().isEmpty()) setStatusText_(QStringLiteral("Select a session and strategy"));
        else setStatusText_(QStringLiteral("Watching %1 result%2").arg(static_cast<qulonglong>(records_.size())).arg(records_.size() == 1u ? QString{} : QStringLiteral("s")));
    }
    const QStringList watchedFiles = watcher_.files();
    if (!watchedFiles.empty()) {
        QStringList filesToRemove;
        for (const QString& file : watchedFiles) {
            if (!filesToWatch.contains(file)) filesToRemove.push_back(file);
        }
        if (!filesToRemove.empty()) (void)watcher_.removePaths(filesToRemove);
    }
    if (!filesToWatch.empty()) {
        const QStringList currentFiles = watcher_.files();
        QStringList filesToAdd;
        for (const QString& file : filesToWatch) {
            if (!QFileInfo::exists(file) || currentFiles.contains(file)) continue;
            filesToAdd.push_back(file);
        }
        if (!filesToAdd.empty()) (void)watcher_.addPaths(filesToAdd);
    }
    emit runsChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    ensureSelectedPreviewLoaded_();
}

void BacktestViewModel::selectRun(const QString& runId) {
    if (selectedRunId_ == runId) return;
    if (RunRecord* oldRecord = mutableRecordForRunId_(selectedRunId_)) clearRecordDetails_(*oldRecord);
    ++previewLoadGeneration_;
    ++detailsLoadGeneration_;
    pendingDetailsRunId_.clear();
    selectedDetailsErrorText_.clear();
    setPreviewLoading_(false);
    setDetailsLoading_(false);
    selectedRunId_ = runId;
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
    ensureSelectedPreviewLoaded_();
}

void BacktestViewModel::loadSelectedRunDetails() {
    RunRecord* record = mutableRecordForRunId_(selectedRunId_);
    if (record == nullptr || record->detailsLoaded || selectedDetailsLoading()) return;
    const QString runId = record->runId;
    const QString filePath = record->filePath;
    if (!record->sweep && !record->equityPoints.empty()) {
        record->detailsLoaded = true;
        emit runsChanged();
        emit selectionChanged();
        emit selectedResultMetricChanged();
        return;
    }
    if (!record->sweep && selectedPreviewLoading()) {
        pendingDetailsRunId_ = runId;
        setDetailsLoading_(true, runId);
        return;
    }
    const std::uint64_t generation = ++detailsLoadGeneration_;
    selectedDetailsErrorText_.clear();
    setDetailsLoading_(true, runId);

    QPointer<BacktestViewModel> self(this);
    std::thread([self, generation, runId, filePath]() {
        RunRecord loaded = BacktestViewModel::loadRecord_(filePath, RecordLoadMode::Details);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, generation, runId, loaded = std::move(loaded)]() {
            if (self) self->applyLoadedDetails_(generation, runId, loaded);
        }, Qt::QueuedConnection);
    }).detach();
}

void BacktestViewModel::unloadSelectedRunDetails() {
    RunRecord* record = mutableRecordForRunId_(selectedRunId_);
    if (record == nullptr) return;
    clearRecordDetails_(*record);
    selectedDetailsErrorText_.clear();
    ++detailsLoadGeneration_;
    pendingDetailsRunId_.clear();
    setDetailsLoading_(false);
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
}

void BacktestViewModel::setSelectedResultScope(const QString& scope) {
    const QString next = scope.trimmed().isEmpty() ? QStringLiteral("portfolio") : scope.trimmed();
    if (selectedResultScope_ == next) return;
    selectedResultScope_ = next;
    if (selectedResultMetricRatioKey_ == selectedResultMetricKey_) selectedResultMetricRatioKey_.clear();
    emit selectedResultScopeChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
}

void BacktestViewModel::setSelectedResultMetricKey(const QString& key) {
    const QString next = key.trimmed();
    if (selectedResultMetricKey_ == next) return;
    selectedResultMetricKey_ = next;
    if (selectedResultMetricRatioKey_ == selectedResultMetricKey_) selectedResultMetricRatioKey_.clear();
    emit selectedResultMetricChanged();
}

void BacktestViewModel::setSelectedResultMetricRatioKey(const QString& key) {
    QString next = key.trimmed();
    if (next == selectedResultMetricKey()) next.clear();
    if (selectedResultMetricRatioKey_ == next) return;
    selectedResultMetricRatioKey_ = next;
    emit selectedResultMetricChanged();
}

bool BacktestViewModel::deleteSelectedRun() {
    const RunRecord* record = selectedRecord_();
    if (record == nullptr || running_) return false;

    const QDir backtestsDir(backtestsDirectory());
    const QString backtestsPath = QDir::cleanPath(backtestsDir.absolutePath());
    const QString runPath = QDir::cleanPath(QFileInfo(record->filePath).absoluteFilePath());
    const QString backtestsPrefix = backtestsPath.endsWith(QLatin1Char('/')) ? backtestsPath : backtestsPath + QLatin1Char('/');
    if (backtestsPath.isEmpty() || runPath == backtestsPath || !runPath.startsWith(backtestsPrefix)) {
        setStatusText_(QStringLiteral("Refusing to delete backtest outside selected session"));
        return false;
    }

    const QString deletedRunId = selectedRunId_;
    if (!QDir(runPath).removeRecursively()) {
        setStatusText_(QStringLiteral("Failed to delete backtest %1").arg(deletedRunId));
        return false;
    }

    selectedRunId_.clear();
    refresh();
    reloadSessions();
    setStatusText_(QStringLiteral("Deleted backtest %1").arg(deletedRunId));
    return true;
}

BacktestViewModel::RunRecord BacktestViewModel::loadRecord_(const QString& filePath, RecordLoadMode mode) {
    RunRecord record;
    const QFileInfo info(filePath);
    const QDir runDir(info.absoluteFilePath());
    const QString manifestPath = runDir.absoluteFilePath(QStringLiteral("manifest.json"));
    record.filePath = info.absoluteFilePath();
    record.fileName = info.fileName();
    record.runId = info.fileName();
    record.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    record.manifestPath = manifestPath;
    record.manifestModifiedMs = fileStampMs_(manifestPath, &record.manifestSize);
    record.modifiedMs = std::max(record.modifiedMs, record.manifestModifiedMs);

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        record.status = QStringLiteral("unreadable");
        record.errorText = QStringLiteral("failed to open result manifest");
        return record;
    }
    const QByteArray raw = file.readAll();
    record.rawJson = QString::fromUtf8(raw);

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        record.status = QStringLiteral("invalid_json");
        record.errorText = parseError.errorString();
        return record;
    }

    const QJsonObject object = doc.object();
    record.valid = true;
    if (jsonValueString(object, QStringLiteral("type")) == QStringLiteral("sweep.result.v1")) {
        const QString sweepId = jsonValueString(object, QStringLiteral("sweep_id"));
        if (!sweepId.trimmed().isEmpty()) record.runId = sweepId.trimmed();
        record.sweep = true;
        record.status = errorCount(object.value(QStringLiteral("errors"))) == 0 ? QStringLiteral("complete") : QStringLiteral("error");
        record.strategy = jsonValueString(object, QStringLiteral("strategy"));
        record.displayName = QStringLiteral("Sweep %1").arg(record.strategy).trimmed();
        record.configText = QStringLiteral("budget=%1 seed=%2 points=%3")
            .arg(jsonValueString(object, QStringLiteral("budget")),
                 jsonValueString(object, QStringLiteral("search_seed")),
                 jsonValueString(object, QStringLiteral("points_evaluated")));
        record.sweepParamKeys = sweepParamKeysFromManifest(object);
        const QJsonObject rowsObject = object.value(QStringLiteral("rows")).toObject();
        QString rowsPath = rowsObject.value(QStringLiteral("path")).toString(QStringLiteral("sweep_results.jsonl"));
        const QFileInfo rowsInfo(rowsPath);
        if (rowsInfo.isRelative()) rowsPath = runDir.absoluteFilePath(rowsPath);
        record.sweepRowsPath = rowsPath;
        record.sweepRowsModifiedMs = fileStampMs_(rowsPath, &record.sweepRowsSize);
        record.modifiedMs = std::max(record.modifiedMs, record.sweepRowsModifiedMs);
        const QJsonObject curvesObject = object.value(QStringLiteral("curves")).toObject();
        QString curvesPath = curvesObject.value(QStringLiteral("path")).toString(QStringLiteral("sweep_curves.jsonl"));
        const QFileInfo curvesInfo(curvesPath);
        if (curvesInfo.isRelative()) curvesPath = runDir.absoluteFilePath(curvesPath);
        record.sweepCurvesPath = curvesPath;
        record.sweepCurvesModifiedMs = fileStampMs_(curvesPath, &record.sweepCurvesSize);
        record.modifiedMs = std::max(record.modifiedMs, record.sweepCurvesModifiedMs);
        if (mode == RecordLoadMode::Details) {
            record.sweepRows = sweepRowsFromJsonl(rowsPath, QStringLiteral("total_pnl_e8"));
            appendSweepParamKeysFromRows(record.sweepRows, record.sweepParamKeys);
            record.sweepCurves = sweepCurvesFromJsonl(curvesPath);
            if (!record.sweepRows.empty()) record.totalPnlE8 = record.sweepRows.front().toMap().value(QStringLiteral("totalPnlE8")).toLongLong();
            if (!record.sweepCurves.empty()) {
                const QVariantMap first = record.sweepCurves.front().toMap();
                record.initialBalanceE8 = first.value(QStringLiteral("initialBalanceE8")).toLongLong();
                record.totalPnlE8 = first.value(QStringLiteral("totalPnlE8")).toLongLong();
            }
            record.detailsLoaded = true;
        }
        record.errorCount = errorCount(object.value(QStringLiteral("errors")));
        record.summaryJson = humanSummaryJson(object);
        record.rawJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        return record;
    }
    const QString runId = jsonValueString(object, QStringLiteral("run_id"));
    if (!runId.trimmed().isEmpty()) record.runId = runId.trimmed();
    record.status = jsonValueString(object, QStringLiteral("status"));
    if (record.status.trimmed().isEmpty()) record.status = QStringLiteral("unknown");
    record.strategy = jsonValueString(object, QStringLiteral("strategy"));
    const QString configPath = jsonValueString(object, QStringLiteral("config_path"));
    if (!configPath.trimmed().isEmpty()) {
        const QFileInfo configInfo(configPath);
        const QString resolvedConfigPath = configInfo.isRelative() ? runDir.absoluteFilePath(configPath) : configPath;
        const QString configText = readTextFile(resolvedConfigPath);
        record.displayName = metadataCommentValue(configText, QStringLiteral("display_name"));
        record.configText = metadataCommentValue(configText, QStringLiteral("config_summary"));
    }
    if (record.displayName.isEmpty()) record.displayName = jsonValueString(object, QStringLiteral("display_name"));
    if (record.configText.isEmpty()) record.configText = jsonValueString(object, QStringLiteral("config_summary"));
    const QJsonObject summary = object.value(QStringLiteral("summary")).toObject();
    record.initialBalanceE8 = summary.value(QStringLiteral("initial_balance_e8")).toInteger();
    record.totalPnlE8 = summary.value(QStringLiteral("net_realized_pnl_e8")).toInteger(
        summary.value(QStringLiteral("realized_pnl_e8")).toInteger(summary.value(QStringLiteral("total_pnl_e8")).toInteger()));
    record.pnlText = pnlPercentText(record.totalPnlE8, record.initialBalanceE8);
    record.summaryJson = humanSummaryJson(object.value(QStringLiteral("summary")));
    const QJsonObject streams = object.value(QStringLiteral("streams")).toObject();
    const QJsonObject equityStream = streams.value(QStringLiteral("equity")).toObject();
    const qint64 equityRows = equityStream.value(QStringLiteral("rows")).toInteger();
    record.equityPath = runDir.absoluteFilePath(QStringLiteral("equity.jsonl"));
    record.equityModifiedMs = fileStampMs_(record.equityPath, &record.equitySize);
    record.modifiedMs = std::max(record.modifiedMs, record.equityModifiedMs);
    record.resultMetrics = resultMetrics(object, summary);
    QVariantMap portfolioScope;
    portfolioScope.insert(QStringLiteral("id"), QStringLiteral("portfolio"));
    portfolioScope.insert(QStringLiteral("label"), QStringLiteral("Portfolio"));
    portfolioScope.insert(QStringLiteral("initialBalanceE8"), record.initialBalanceE8);
    portfolioScope.insert(QStringLiteral("totalPnlE8"), record.totalPnlE8);
    record.resultScopes.push_back(portfolioScope);
    record.scopedResultMetrics.insert(QStringLiteral("portfolio"), record.resultMetrics);
    record.scopedInitialBalanceE8.insert(QStringLiteral("portfolio"), record.initialBalanceE8);
    if (mode != RecordLoadMode::MetadataOnly) {
        record.equityPoints = equityPointsFromJsonl(record.equityPath, summary, equityRows, record.pnlMinE8, record.pnlMaxE8);
        record.scopedEquityPoints.insert(QStringLiteral("portfolio"), record.equityPoints);
        record.scopedPnlMinE8.insert(QStringLiteral("portfolio"), record.pnlMinE8);
        record.scopedPnlMaxE8.insert(QStringLiteral("portfolio"), record.pnlMaxE8);
    }
    const QJsonArray legs = object.value(QStringLiteral("legs")).toArray();
    std::vector<QVariantList> legEquitySeries;
    if (mode != RecordLoadMode::MetadataOnly) legEquitySeries.reserve(static_cast<std::size_t>(legs.size()));
    for (int i = 0; i < legs.size(); ++i) {
        if (!legs.at(i).isObject()) continue;
        const QJsonObject leg = legs.at(i).toObject();
        const QString scopeId = QStringLiteral("leg_%1").arg(i);
        const QString exchange = leg.value(QStringLiteral("exchange")).toString();
        const QString market = leg.value(QStringLiteral("market")).toString();
        const QString symbol = leg.value(QStringLiteral("symbol")).toString();
        QVariantMap scope;
        scope.insert(QStringLiteral("id"), scopeId);
        scope.insert(QStringLiteral("label"), QStringLiteral("Leg %1 %2 %3 %4").arg(i + 1).arg(exchange, market, symbol).trimmed());
        scope.insert(QStringLiteral("index"), i);
        scope.insert(QStringLiteral("exchange"), exchange);
        scope.insert(QStringLiteral("market"), market);
        scope.insert(QStringLiteral("symbol"), symbol);
        scope.insert(QStringLiteral("initialBalanceE8"), leg.value(QStringLiteral("initial_balance_e8")).toInteger());
        scope.insert(QStringLiteral("totalPnlE8"), leg.value(QStringLiteral("total_pnl_e8")).toInteger());
        record.resultScopes.push_back(scope);
        record.scopedResultMetrics.insert(scopeId, resultMetrics(object, leg));
        record.scopedInitialBalanceE8.insert(scopeId, leg.value(QStringLiteral("initial_balance_e8")).toInteger());
        if (mode != RecordLoadMode::MetadataOnly) {
            const QJsonObject legEquity = leg.value(QStringLiteral("equity")).toObject();
            QString legEquityPath = legEquity.value(QStringLiteral("path")).toString();
            if (legEquityPath.isEmpty()) legEquityPath = QStringLiteral("legs/%1/equity.jsonl").arg(i);
            if (QFileInfo(legEquityPath).isRelative()) legEquityPath = runDir.absoluteFilePath(legEquityPath);
            qint64 minPnl = 0;
            qint64 maxPnl = 0;
            const QVariantList points = equityPointsFromJsonl(legEquityPath, leg, legEquity.value(QStringLiteral("rows")).toInteger(), minPnl, maxPnl);
            legEquitySeries.push_back(points);
            record.scopedEquityPoints.insert(scopeId, points);
            record.scopedPnlMinE8.insert(scopeId, minPnl);
            record.scopedPnlMaxE8.insert(scopeId, maxPnl);
        }
    }
    if (mode != RecordLoadMode::MetadataOnly && !legEquitySeries.empty()) {
        qint64 minPnl = 0;
        qint64 maxPnl = 0;
        const QVariantList points = synthesizePortfolioEquityPoints(legEquitySeries, minPnl, maxPnl);
        if (points.size() >= 2 && points.size() > record.equityPoints.size()) {
            record.equityPoints = points;
            record.pnlMinE8 = minPnl;
            record.pnlMaxE8 = maxPnl;
            record.scopedEquityPoints.insert(QStringLiteral("portfolio"), record.equityPoints);
            record.scopedPnlMinE8.insert(QStringLiteral("portfolio"), record.pnlMinE8);
            record.scopedPnlMaxE8.insert(QStringLiteral("portfolio"), record.pnlMaxE8);
        }
    }
    if (mode == RecordLoadMode::Details) {
        record.detailsLoaded = true;
    }
    record.errorCount = errorCount(object.value(QStringLiteral("errors")));
    record.rawJson = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    return record;
}

QString BacktestViewModel::normalizedPath_(const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return {};
    return QDir::cleanPath(QDir(trimmed).absolutePath());
}

QString BacktestViewModel::sessionIdFromPath_(const QString& path) {
    return QFileInfo(path).fileName();
}

const BacktestViewModel::RunRecord* BacktestViewModel::selectedRecord_() const noexcept {
    const auto it = std::find_if(records_.begin(), records_.end(), [this](const RunRecord& record) {
        return record.runId == selectedRunId_;
    });
    return it == records_.end() ? nullptr : &(*it);
}

const BacktestViewModel::RunRecord* BacktestViewModel::recordForPath_(const QString& filePath) const noexcept {
    const QString target = QFileInfo(filePath).absoluteFilePath();
    const auto it = std::find_if(records_.begin(), records_.end(), [&target](const RunRecord& record) {
        return record.filePath == target;
    });
    return it == records_.end() ? nullptr : &(*it);
}

BacktestViewModel::RunRecord* BacktestViewModel::mutableRecordForRunId_(const QString& runId) noexcept {
    const auto it = std::find_if(records_.begin(), records_.end(), [&runId](const RunRecord& record) {
        return record.runId == runId;
    });
    return it == records_.end() ? nullptr : &(*it);
}

QString BacktestViewModel::effectiveResultScopeId_(const RunRecord& record) const {
    const QString requested = selectedResultScope_.trimmed().isEmpty() ? QStringLiteral("portfolio") : selectedResultScope_.trimmed();
    for (const QVariant& value : record.resultScopes) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (id == requested) return requested;
    }
    return QStringLiteral("portfolio");
}

void BacktestViewModel::scheduleRefresh_() {
    refreshTimer_.start();
}

qint64 BacktestViewModel::fileStampMs_(const QString& path, qint64* sizeOut) {
    if (sizeOut != nullptr) *sizeOut = -1;
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) return 0;
    const QFileInfo info(trimmed);
    if (!info.exists()) return 0;
    if (sizeOut != nullptr) *sizeOut = info.size();
    return info.lastModified().toMSecsSinceEpoch();
}

bool BacktestViewModel::fileStampMatches_(const QString& path, qint64 modifiedMs, qint64 size) {
    qint64 currentSize = -1;
    return fileStampMs_(path, &currentSize) == modifiedMs && currentSize == size;
}

void BacktestViewModel::updateWatcher_() {
    QStringList desiredDirs;
    const QString dirPath = backtestsDirectory();
    if (!dirPath.isEmpty()) {
        QDir sessionDir(selectedSessionPath());
        if (sessionDir.exists()) {
            sessionDir.mkpath(QStringLiteral("backtests"));
            if (QDir(dirPath).exists()) desiredDirs.push_back(dirPath);
        }
    }

    const QStringList watchedDirs = watcher_.directories();
    if (!watchedDirs.empty()) {
        QStringList dirsToRemove;
        for (const QString& dir : watchedDirs) {
            if (!desiredDirs.contains(dir)) dirsToRemove.push_back(dir);
        }
        if (!dirsToRemove.empty()) (void)watcher_.removePaths(dirsToRemove);
    }
    if (!desiredDirs.empty()) {
        QStringList dirsToAdd;
        for (const QString& dir : desiredDirs) {
            if (!watchedDirs.contains(dir)) dirsToAdd.push_back(dir);
        }
        if (!dirsToAdd.empty()) (void)watcher_.addPaths(dirsToAdd);
    }
}

void BacktestViewModel::setStatusText_(const QString& statusText) {
    if (statusText_ == statusText) return;
    statusText_ = statusText;
    emit statusTextChanged();
}

void BacktestViewModel::refreshSessionGateStatus_() {
    if (!strategySupportsSelectedSessionCount_()) {
        const QString gateText = strategySessionGateText(selectedStrategy_, selectedSessionCount());
        if (!gateText.isEmpty()) setStatusText_(gateText);
        return;
    }
    if (!statusText_.startsWith(QStringLiteral("Selected ")) ||
        !statusText_.contains(QStringLiteral("strategy supports"))) {
        return;
    }
    if (selectedSessionPath().isEmpty()) {
        setStatusText_(QStringLiteral("Select a session and strategy"));
    } else {
        setStatusText_(QStringLiteral("Watching %1 result%2")
                           .arg(static_cast<qulonglong>(records_.size()))
                           .arg(records_.size() == 1u ? QString{} : QStringLiteral("s")));
    }
}

void BacktestViewModel::setRunning_(bool running) {
    if (running_ == running) return;
    running_ = running;
    emit runningChanged();
    emit canRunChanged();
}

void BacktestViewModel::setProgress_(int percent, const QString& text) {
    const int clamped = std::max(0, std::min(100, percent));
    if (progressPercent_ == clamped && progressText_ == text) return;
    progressPercent_ = clamped;
    progressText_ = text;
    emit progressChanged();
}

void BacktestViewModel::setPreviewLoading_(bool loading, const QString& runId) {
    if (previewLoading_ == loading && previewLoadingRunId_ == runId) return;
    previewLoading_ = loading;
    previewLoadingRunId_ = loading ? runId : QString{};
    emit previewLoadingChanged();
}

void BacktestViewModel::setDetailsLoading_(bool loading, const QString& runId) {
    if (detailsLoading_ == loading && detailsLoadingRunId_ == runId) return;
    detailsLoading_ = loading;
    detailsLoadingRunId_ = loading ? runId : QString{};
    emit detailsLoadingChanged();
}

void BacktestViewModel::clearRecordDetails_(RunRecord& record) {
    record.sweepRows.clear();
    record.sweepCurves.clear();
    record.scopedEquityPoints.clear();
    record.scopedPnlMinE8.clear();
    record.scopedPnlMaxE8.clear();
    record.detailsErrorText.clear();
    record.detailsLoaded = false;
}

void BacktestViewModel::ensureSelectedPreviewLoaded_() {
    const RunRecord* record = selectedRecord_();
    if (record == nullptr || record->sweep || !record->valid || !record->equityPoints.empty() || selectedPreviewLoading()) return;
    const QString runId = record->runId;
    const QString filePath = record->filePath;
    const std::uint64_t generation = ++previewLoadGeneration_;
    setPreviewLoading_(true, runId);

    QPointer<BacktestViewModel> self(this);
    std::thread([self, generation, runId, filePath]() {
        RunRecord loaded = BacktestViewModel::loadRecord_(filePath, RecordLoadMode::Preview);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, generation, runId, loaded = std::move(loaded)]() {
            if (self) self->applyLoadedPreview_(generation, runId, loaded);
        }, Qt::QueuedConnection);
    }).detach();
}

void BacktestViewModel::applyLoadedPreview_(std::uint64_t generation, const QString& runId, const RunRecord& loaded) {
    if (generation != previewLoadGeneration_ || selectedRunId_ != runId) return;
    setPreviewLoading_(false);
    RunRecord* record = mutableRecordForRunId_(runId);
    if (record == nullptr || record->filePath != loaded.filePath) return;

    record->equityPoints = loaded.equityPoints;
    record->resultScopes = loaded.resultScopes;
    record->scopedEquityPoints = loaded.scopedEquityPoints;
    record->scopedResultMetrics = loaded.scopedResultMetrics;
    record->scopedInitialBalanceE8 = loaded.scopedInitialBalanceE8;
    record->scopedPnlMinE8 = loaded.scopedPnlMinE8;
    record->scopedPnlMaxE8 = loaded.scopedPnlMaxE8;
    record->pnlMinE8 = loaded.pnlMinE8;
    record->pnlMaxE8 = loaded.pnlMaxE8;
    record->equityModifiedMs = loaded.equityModifiedMs;
    record->equitySize = loaded.equitySize;
    if (pendingDetailsRunId_ == runId) {
        record->detailsLoaded = loaded.valid;
        pendingDetailsRunId_.clear();
        setDetailsLoading_(false);
    }

    emit runsChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
}

void BacktestViewModel::applyLoadedDetails_(std::uint64_t generation, const QString& runId, const RunRecord& loaded) {
    if (generation != detailsLoadGeneration_ || selectedRunId_ != runId) return;
    setDetailsLoading_(false);
    RunRecord* record = mutableRecordForRunId_(runId);
    if (record == nullptr || record->filePath != loaded.filePath) return;

    record->rawJson = loaded.rawJson;
    record->summaryJson = loaded.summaryJson;
    record->errorText = loaded.errorText;
    record->detailsErrorText = loaded.detailsErrorText;
    record->equityPoints = loaded.equityPoints;
    record->resultScopes = loaded.resultScopes;
    record->resultMetrics = loaded.resultMetrics;
    record->scopedEquityPoints = loaded.scopedEquityPoints;
    record->scopedResultMetrics = loaded.scopedResultMetrics;
    record->scopedInitialBalanceE8 = loaded.scopedInitialBalanceE8;
    record->scopedPnlMinE8 = loaded.scopedPnlMinE8;
    record->scopedPnlMaxE8 = loaded.scopedPnlMaxE8;
    record->sweepRows = loaded.sweepRows;
    record->sweepCurves = loaded.sweepCurves;
    record->sweepParamKeys = loaded.sweepParamKeys;
    record->pnlMinE8 = loaded.pnlMinE8;
    record->pnlMaxE8 = loaded.pnlMaxE8;
    record->initialBalanceE8 = loaded.initialBalanceE8;
    record->totalPnlE8 = loaded.totalPnlE8;
    record->pnlText = loaded.pnlText;
    record->valid = loaded.valid;
    record->detailsLoaded = loaded.valid;
    selectedDetailsErrorText_ = loaded.valid ? loaded.detailsErrorText : loaded.errorText;

    emit runsChanged();
    emit selectionChanged();
    emit selectedResultMetricChanged();
    emit detailsLoadingChanged();
}

}  // namespace hftrec::gui
