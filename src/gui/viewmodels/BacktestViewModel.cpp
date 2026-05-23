#include "gui/viewmodels/BacktestViewModel.hpp"

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
#include <QVariantMap>

#include <algorithm>

#include "hftrec/backtest.hpp"
#include "core/common/Status.hpp"

namespace hftrec::gui {
namespace {

QString jsonValueString(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toInteger());
    return {};
}

QString prettyJson(const QJsonValue& value) {
    if (value.isUndefined()) return {};
    QJsonDocument doc;
    if (value.isObject()) doc = QJsonDocument(value.toObject());
    else if (value.isArray()) doc = QJsonDocument(value.toArray());
    else return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("value"), value}}).toJson(QJsonDocument::Indented));
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

int errorCount(const QJsonValue& value) {
    return value.isArray() ? value.toArray().size() : 0;
}

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString statusTextFor(hftrec::Status status) {
    if (status == hftrec::Status::Ok) return QStringLiteral("Backtest complete");
    if (status == hftrec::Status::Cancelled) return QStringLiteral("Backtest cancelled");
    return QStringLiteral("Backtest failed: %1").arg(QString::fromUtf8(hftrec::statusToString(status).data(), static_cast<qsizetype>(hftrec::statusToString(status).size())));
}

bool progressCallback(const hftrec::BacktestProgress& progress, void* userData) noexcept {
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

}  // namespace

BacktestViewModel::BacktestViewModel(QObject* parent) : QObject(parent) {
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this]() { refresh(); });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this]() { refresh(); });
    const QVariantList rows = sessions();
    if (!rows.empty()) selectedSessionId_ = rows.front().toMap().value(QStringLiteral("id")).toString();
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
    QVariantList out;
    QDir recordingsDir(recordingsRoot());
    if (!recordingsDir.exists()) return out;

    const auto entries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    for (const auto& entry : entries) {
        const QString path = recordingsDir.absoluteFilePath(entry);
        QVariantMap row;
        row.insert(QStringLiteral("id"), entry);
        row.insert(QStringLiteral("label"), entry);
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("hasManifest"), QFileInfo::exists(QDir(path).absoluteFilePath(QStringLiteral("manifest.json"))));
        row.insert(QStringLiteral("hasBacktests"), QDir(QDir(path).absoluteFilePath(QStringLiteral("backtests"))).exists());
        out.push_back(row);
    }
    return out;
}

QString BacktestViewModel::selectedSessionPath() const {
    if (!manualSessionPath_.trimmed().isEmpty()) return manualSessionPath_;
    if (selectedSessionId_.trimmed().isEmpty()) return {};
    return QDir(recordingsRoot()).absoluteFilePath(selectedSessionId_);
}

QString BacktestViewModel::backtestsDirectory() const {
    const QString path = selectedSessionPath();
    return path.isEmpty() ? QString{} : QDir(path).absoluteFilePath(QStringLiteral("backtests"));
}

QVariantList BacktestViewModel::strategyChoices() const {
    static constexpr const char* kStrategies[] = {
        "spread_maker1and2",
        "horizontal_levels",
        "iceberg_detector",
        "liquidity_wall_breakout",
        "liquidity_wall_rebound",
        "liquidity_volume_maker",
    };
    QVariantList out;
    for (const char* strategy : kStrategies) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), QString::fromUtf8(strategy));
        row.insert(QStringLiteral("label"), QString::fromUtf8(strategy));
        out.push_back(row);
    }
    return out;
}

QVariantList BacktestViewModel::runs() const {
    QVariantList out;
    for (const auto& record : records_) {
        QVariantMap row;
        row.insert(QStringLiteral("runId"), record.runId);
        row.insert(QStringLiteral("status"), record.status);
        row.insert(QStringLiteral("strategy"), record.strategy);
        row.insert(QStringLiteral("filePath"), record.filePath);
        row.insert(QStringLiteral("fileName"), record.fileName);
        row.insert(QStringLiteral("modifiedText"), QDateTime::fromMSecsSinceEpoch(record.modifiedMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        row.insert(QStringLiteral("errorCount"), record.errorCount);
        row.insert(QStringLiteral("valid"), record.valid);
        out.push_back(row);
    }
    return out;
}

QString BacktestViewModel::selectedJson() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->rawJson;
}

QString BacktestViewModel::selectedSummaryJson() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->summaryJson;
}

QString BacktestViewModel::selectedErrorText() const {
    const auto* record = selectedRecord_();
    return record == nullptr ? QString{} : record->errorText;
}

bool BacktestViewModel::canRun() const {
    return !running_ && !selectedSessionPath().trimmed().isEmpty() && !selectedStrategy_.trimmed().isEmpty();
}

void BacktestViewModel::reloadSessions() {
    emit sessionsChanged();
    emit canRunChanged();
}

void BacktestViewModel::setSelectedSessionId(const QString& sessionId) {
    const QString next = sessionId.trimmed();
    if (selectedSessionId_ == next && manualSessionPath_.isEmpty()) return;
    selectedSessionId_ = next;
    manualSessionPath_.clear();
    selectedRunId_.clear();
    emit selectedSessionChanged();
    emit canRunChanged();
    refresh();
}

void BacktestViewModel::setSessionPath(const QString& sessionPath) {
    const QString normalized = normalizedPath_(sessionPath);
    if (selectedSessionPath() == normalized) return;
    manualSessionPath_ = normalized;
    selectedSessionId_ = sessionIdFromPath_(normalized);
    selectedRunId_.clear();
    emit selectedSessionChanged();
    emit canRunChanged();
    refresh();
}

void BacktestViewModel::setSelectedStrategy(const QString& strategy) {
    const QString next = strategy.trimmed();
    if (selectedStrategy_ == next || next.isEmpty()) return;
    selectedStrategy_ = next;
    emit selectedStrategyChanged();
    emit canRunChanged();
}

void BacktestViewModel::refresh() {
    updateWatcher_();
    std::vector<RunRecord> next;
    const QString dirPath = backtestsDirectory();
    QStringList filesToWatch;
    if (!dirPath.isEmpty()) {
        QDir dir(dirPath);
        const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time | QDir::Name);
        next.reserve(static_cast<std::size_t>(files.size()));
        for (const QFileInfo& file : files) {
            filesToWatch.push_back(file.absoluteFilePath());
            next.push_back(loadRecord_(file.absoluteFilePath()));
        }
    }

    std::sort(next.begin(), next.end(), [](const RunRecord& lhs, const RunRecord& rhs) {
        if (lhs.modifiedMs != rhs.modifiedMs) return lhs.modifiedMs > rhs.modifiedMs;
        return lhs.fileName < rhs.fileName;
    });
    records_ = std::move(next);

    if (!selectedRunId_.isEmpty() && selectedRecord_() == nullptr) selectedRunId_.clear();
    if (selectedRunId_.isEmpty() && !records_.empty()) selectedRunId_ = records_.front().runId;

    if (!running_) {
        if (selectedSessionPath().isEmpty()) setStatusText_(QStringLiteral("Select a session and strategy"));
        else setStatusText_(QStringLiteral("Watching %1 result%2").arg(static_cast<qulonglong>(records_.size())).arg(records_.size() == 1u ? QString{} : QStringLiteral("s")));
    }
    if (!filesToWatch.empty()) (void)watcher_.addPaths(filesToWatch);
    emit runsChanged();
    emit selectionChanged();
}

void BacktestViewModel::selectRun(const QString& runId) {
    if (selectedRunId_ == runId) return;
    selectedRunId_ = runId;
    emit selectionChanged();
}

void BacktestViewModel::startBacktest() {
    if (!canRun()) return;
    stopWorker_();
    cancelRequested_.store(false, std::memory_order_release);
    setRunning_(true);
    setProgress_(0, QStringLiteral("Starting"));
    setStatusText_(QStringLiteral("Backtest running"));

    const QString sessionPath = selectedSessionPath();
    const QString strategy = selectedStrategy_;
    const QString runId = runId_();
    worker_ = std::thread([this, sessionPath, strategy, runId] {
        hftrec::BacktestRunRequest request{};
        request.sessionPath = sessionPath.toStdString();
        request.strategy = strategy.toStdString();
        request.runId = runId.toStdString();
        request.requestId = runId.toStdString();

        const auto result = hftrec::runBacktest(request, progressCallback, this);
        const QString status = statusTextFor(result.status);
        const QString selected = QString::fromStdString(result.runId);
        QMetaObject::invokeMethod(this, [this, status, selected] {
            setRunning_(false);
            setProgress_(100, status);
            setStatusText_(status);
            refresh();
            if (!selected.isEmpty()) selectRun(selected);
        }, Qt::QueuedConnection);
    });
}

void BacktestViewModel::cancelBacktest() {
    cancelRequested_.store(true, std::memory_order_release);
    if (running_) setStatusText_(QStringLiteral("Cancelling backtest"));
}

void BacktestViewModel::applyWorkerProgress(int percent, const QString& text) {
    setProgress_(percent, text);
}

BacktestViewModel::RunRecord BacktestViewModel::loadRecord_(const QString& filePath) {
    RunRecord record;
    const QFileInfo info(filePath);
    record.filePath = info.absoluteFilePath();
    record.fileName = info.fileName();
    record.runId = info.completeBaseName();
    record.modifiedMs = info.lastModified().toMSecsSinceEpoch();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        record.status = QStringLiteral("unreadable");
        record.errorText = QStringLiteral("failed to open result file");
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
    const QString runId = jsonValueString(object, QStringLiteral("run_id"));
    if (!runId.trimmed().isEmpty()) record.runId = runId.trimmed();
    record.status = jsonValueString(object, QStringLiteral("status"));
    if (record.status.trimmed().isEmpty()) record.status = QStringLiteral("unknown");
    record.strategy = jsonValueString(object, QStringLiteral("strategy"));
    record.summaryJson = prettyJson(object.value(QStringLiteral("summary")));
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

void BacktestViewModel::updateWatcher_() {
    const QStringList watchedFiles = watcher_.files();
    if (!watchedFiles.empty()) (void)watcher_.removePaths(watchedFiles);
    const QStringList watchedDirs = watcher_.directories();
    if (!watchedDirs.empty()) (void)watcher_.removePaths(watchedDirs);

    const QString dirPath = backtestsDirectory();
    if (dirPath.isEmpty()) return;
    QDir sessionDir(selectedSessionPath());
    if (!sessionDir.exists()) return;
    sessionDir.mkpath(QStringLiteral("backtests"));
    if (QDir(dirPath).exists()) (void)watcher_.addPath(dirPath);
}

void BacktestViewModel::setStatusText_(const QString& statusText) {
    if (statusText_ == statusText) return;
    statusText_ = statusText;
    emit statusTextChanged();
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

void BacktestViewModel::stopWorker_() {
    if (worker_.joinable()) worker_.join();
}

QString BacktestViewModel::runId_() const {
    return QStringLiteral("run-%1").arg(QDateTime::currentMSecsSinceEpoch());
}

}  // namespace hftrec::gui
