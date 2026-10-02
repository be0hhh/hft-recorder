#include "CompressionViewModel.hpp"

#include "CompressionViewModelHelpers.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QTextStream>
#include <QVariantMap>
#include <QDirIterator>

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <thread>
#include <vector>

#include "hft_compressor/Compressor.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"
#include "../../Backtests/Sessions/BacktestSessionSummary.hpp"
#include "../../Models/RecordingCatalog.hpp"

namespace hftrec::gui {


using namespace compression_vm;

QString CompressionViewModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

QObject* CompressionViewModel::recordingCatalog() const {
    return recordingCatalog_;
}

void CompressionViewModel::setRecordingCatalog(QObject* recordingCatalog) {
    auto* typedCatalog = qobject_cast<RecordingCatalog*>(recordingCatalog);
    if (typedCatalog == recordingCatalog_) return;
    recordingCatalog_ = typedCatalog;
    reconnectRecordingCatalog_();
    initializeSelectedSessionFromCatalog_();
    emit recordingCatalogChanged();
}

QVariantList CompressionViewModel::sessions() const {
    QVariantList out;
    if (recordingCatalog_ == nullptr || !recordingCatalog_->hasSnapshot()) return out;
    const auto& snapshot = recordingCatalog_->snapshot();
    for (const auto& session : snapshot.discovery.sessions) {
        const QString id = QString::fromStdString(session.sessionId);
        const QString path = QString::fromStdString(session.path.string());
        const BacktestLegCounts counts = snapshot.backtestCountsBySession.value(id);
        QString summary = sessionBacktestSummaryText(static_cast<int>(session.bookTickerCount), counts, session.startedAtNs);
        if (session.candleCount > 0) summary += QStringLiteral(" | C %1").arg(session.candleCount);
        summary = appendSessionHealthSummary(summary,
                                             QString::fromStdString(session.sessionHealth),
                                             QString::fromStdString(session.warningSummary));
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), QStringLiteral("%1 | %2/%3 %4")
                                            .arg(QString::fromStdString(session.groupTitle),
                                                 QString::fromStdString(session.exchange),
                                                 QString::fromStdString(session.market),
                                                 QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front())));
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("rightText"), summary);
        row.insert(QStringLiteral("hasTrades"), !existingChannelPath_(path, QStringLiteral("trades")).isEmpty());
        row.insert(QStringLiteral("hasBookTicker"), !existingChannelPath_(path, QStringLiteral("bookticker")).isEmpty());
        row.insert(QStringLiteral("hasDepth"), !existingChannelPath_(path, QStringLiteral("depth")).isEmpty());
        out.push_back(row);
    }
    return out;
}

bool CompressionViewModel::hasSessions() const {
    return !sessions().empty();
}

QString CompressionViewModel::selectedSessionPath() const {
    if (selectedSessionId_.trimmed().isEmpty()) return {};
    const auto rows = sessions();
    for (const auto& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        if (row.value(QStringLiteral("id")).toString() == selectedSessionId_) {
            return row.value(QStringLiteral("path")).toString();
        }
    }
    return QDir(recordingsRoot()).absoluteFilePath(selectedSessionId_);
}

void CompressionViewModel::reconnectRecordingCatalog_() {
    if (catalogSnapshotConnection_) disconnect(catalogSnapshotConnection_);
    if (recordingCatalog_ == nullptr) return;
    catalogSnapshotConnection_ =
        connect(recordingCatalog_, &RecordingCatalog::snapshotChanged, this, &CompressionViewModel::initializeSelectedSessionFromCatalog_);
}

void CompressionViewModel::initializeSelectedSessionFromCatalog_() {
    const auto rows = sessions();
    bool selectedStillExists = false;
    for (const auto& rowValue : rows) {
        if (rowValue.toMap().value(QStringLiteral("id")).toString() == selectedSessionId_) {
            selectedStillExists = true;
            break;
        }
    }
    const bool sessionChanged = !selectedStillExists && (!rows.empty() || !selectedSessionId_.isEmpty());
    if (sessionChanged) {
        selectedSessionId_ = rows.empty() ? QString{} : rows.front().toMap().value(QStringLiteral("id")).toString();
        const QString firstChannel = firstAvailableChannel_(selectedSessionPath());
        selectedChannel_ = firstChannel.isEmpty() ? QStringLiteral("trades") : firstChannel;
        emit selectedSessionChanged();
        emit selectedChannelChanged();
    }
    reloadStoredRunRows_();
    reloadStoredVerifyRows_();
    emit sessionsChanged();
    emit channelChoicesChanged();
    emit channelStatsChanged();
    emitSelectionChanged_();
}

QVariantList CompressionViewModel::channelChoices() const {
    QVariantList out;
    const QString sessionPath = selectedSessionPath();
    const QString channels[] = {QStringLiteral("trades"), QStringLiteral("bookticker"), QStringLiteral("depth")};
    for (const QString& channel : channels) {
        const QString path = existingChannelPath_(sessionPath, channel);
        QVariantMap row;
        row.insert(QStringLiteral("id"), channel);
        row.insert(QStringLiteral("label"), displayChannel(channel));
        row.insert(QStringLiteral("file"), path.isEmpty() ? preferredChannelPath_(sessionPath, channel) : path);
        row.insert(QStringLiteral("available"), !path.isEmpty());
        out.push_back(row);
    }
    return out;
}

QString CompressionViewModel::firstAvailablePipelineId_() const {
    for (const auto& pipeline : hft_compressor::listPipelines()) {
        if (pipeline.availability != hft_compressor::PipelineAvailability::Available) continue;
        if (pipelineMatchesChannel(pipeline, selectedChannel_)) return viewString(pipeline.id);
    }
    for (const auto& value : pythonCodecPipelineRows()) {
        const QVariantMap row = value.toMap();
        if (variantBool(row, QStringLiteral("available"))) return row.value(QStringLiteral("id")).toString();
    }
    return {};
}

QString CompressionViewModel::inputFile() const {
    if (!manualInputFile_.trimmed().isEmpty() && selectedSessionId_.trimmed().isEmpty()) return manualInputFile_;
    const QString path = existingChannelPath_(selectedSessionPath(), selectedChannel_);
    return path.isEmpty() ? preferredChannelPath_(selectedSessionPath(), selectedChannel_) : path;
}

QString CompressionViewModel::outputRoot() const {
    if (!manualOutputRoot_.trimmed().isEmpty() && selectedSessionId_.trimmed().isEmpty()) return manualOutputRoot_;
    const QString sessionPath = selectedSessionPath();
    if (sessionPath.isEmpty()) return {};
    return QDir(sessionPath).absoluteFilePath(QStringLiteral("compressed"));
}

QVariantList CompressionViewModel::outputRootChoices() const {
    QVariantList out;
    const QString root = outputRoot();
    if (!root.isEmpty()) {
        QVariantMap row;
        row.insert(QStringLiteral("label"), root);
        row.insert(QStringLiteral("path"), root);
        out.push_back(row);
    }
    return out;
}

QVariantList CompressionViewModel::pipelines() const {
    QVariantList out;
    for (const auto& pipeline : hft_compressor::listPipelines()) {
        if (!isCompressTablePipeline(pipeline)) continue;
        if (!pipelineMatchesChannel(pipeline, selectedChannel_)) continue;
        QVariantMap row;
        row.insert(QStringLiteral("id"), viewString(pipeline.id));
        row.insert(QStringLiteral("label"), viewString(pipeline.label));
        row.insert(QStringLiteral("streamScope"), viewString(pipeline.streamScope));
        row.insert(QStringLiteral("representation"), viewString(pipeline.representation));
        row.insert(QStringLiteral("transform"), viewString(pipeline.transform));
        row.insert(QStringLiteral("entropy"), viewString(pipeline.entropy));
        row.insert(QStringLiteral("profile"), viewString(pipeline.profile));
        row.insert(QStringLiteral("profileLabel"), displayProfile(viewString(pipeline.profile)));
        row.insert(QStringLiteral("implementationKind"), viewString(pipeline.implementationKind));
        row.insert(QStringLiteral("group"), groupForPipeline(pipeline));
        row.insert(QStringLiteral("availability"), viewString(hft_compressor::pipelineAvailabilityToString(pipeline.availability)));
        row.insert(QStringLiteral("availabilityReason"), viewString(pipeline.availabilityReason));
        row.insert(QStringLiteral("available"), pipeline.availability == hft_compressor::PipelineAvailability::Available);
        row.insert(QStringLiteral("summary"), pipelineSummary(pipeline));
        out.push_back(row);
    }
    for (const auto& value : pythonCodecPipelineRows()) out.push_back(value);
    return out;
}

QVariantList CompressionViewModel::pipelineGroups() const {
    const QString names[] = {
        QStringLiteral("Standard baselines"),
        QStringLiteral("Domain transforms"),
        QStringLiteral("Python prototypes"),
        QStringLiteral("Custom entropy coders"),
    };
    QVariantList out;
    for (const QString& name : names) {
        QVariantMap row;
        int total = 0;
        int available = 0;
        for (const auto& pipeline : hft_compressor::listPipelines()) {
            if (!isCompressTablePipeline(pipeline)) continue;
            if (groupForPipeline(pipeline) != name) continue;
            ++total;
            if (pipeline.availability == hft_compressor::PipelineAvailability::Available) ++available;
        }
        if (name == QStringLiteral("Python prototypes")) {
            for (const auto& value : pythonCodecPipelineRows()) {
                const QVariantMap pipeline = value.toMap();
                ++total;
                if (variantBool(pipeline, QStringLiteral("available"))) ++available;
            }
        }
        row.insert(QStringLiteral("label"), name);
        row.insert(QStringLiteral("total"), total);
        row.insert(QStringLiteral("available"), available);
        row.insert(QStringLiteral("planned"), total - available);
        row.insert(QStringLiteral("summary"), QStringLiteral("%1 available / %2 planned").arg(available).arg(total - available));
        out.push_back(row);
    }
    return out;
}

QString CompressionViewModel::emptyStateText() const {
    if (!hasSessions()) return QStringLiteral("Нет записанных сессий. Сначала запиши данные во вкладке Capture.");
    if (selectedSessionId_.trimmed().isEmpty()) return QStringLiteral("Выбери сессию, чтобы открыть статистику сжатия.");
    if (firstAvailableChannel_(selectedSessionPath()).isEmpty()) return QStringLiteral("В сессии нет JSONL файлов для сжатия.");
    return QString{};
}

QString CompressionViewModel::selectedPipelineLabel() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    return pipelineLabelFor(selectedPipelineId_);
}

QString CompressionViewModel::selectedPipelineSummary() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    if (pipeline != nullptr) return pipelineSummary(*pipeline);
    return findPythonPipelineRow(selectedPipelineId_).value(QStringLiteral("summary")).toString();
}

bool CompressionViewModel::selectedPipelineAvailable() const {
    if (isPythonPipelineId(selectedPipelineId_)) {
        return variantBool(findPythonPipelineRow(selectedPipelineId_), QStringLiteral("available"));
    }
    const auto* pipeline = findPipeline(selectedPipelineId_);
    if (pipeline == nullptr || pipeline->availability != hft_compressor::PipelineAvailability::Available) return false;
    return pipelineMatchesChannel(*pipeline, selectedChannel_);
}

QString CompressionViewModel::inferredStream() const {
    const auto stream = hft_compressor::inferStreamTypeFromPath(inputFile().toStdString());
    return viewString(hft_compressor::streamTypeToString(stream));
}

void CompressionViewModel::reloadSessions() {
    if (recordingCatalog_ != nullptr) recordingCatalog_->refresh();
    const auto rows = sessions();
    bool selectedStillExists = false;
    for (const auto& rowValue : rows) {
        if (rowValue.toMap().value(QStringLiteral("id")).toString() == selectedSessionId_) {
            selectedStillExists = true;
            break;
        }
    }
    if (!selectedStillExists) {
        selectedSessionId_ = rows.empty() ? QString{} : rows.front().toMap().value(QStringLiteral("id")).toString();
        const QString firstChannel = firstAvailableChannel_(selectedSessionPath());
        selectedChannel_ = firstChannel.isEmpty() ? QStringLiteral("trades") : firstChannel;
        emit selectedSessionChanged();
        emit selectedChannelChanged();
    }
    reloadStoredRunRows_();
    reloadStoredVerifyRows_();
    emit sessionsChanged();
    emit channelChoicesChanged();
    emit channelStatsChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setSelectedSessionId(const QString& sessionId) {
    const QString normalized = sessionId.trimmed();
    if (selectedSessionId_ == normalized) return;
    selectedSessionId_ = normalized;
    const QString firstChannel = firstAvailableChannel_(selectedSessionPath());
    selectedChannel_ = firstChannel.isEmpty() ? QStringLiteral("trades") : firstChannel;
    manualInputFile_.clear();
    manualOutputRoot_.clear();
    if (!selectedPipelineAvailable()) {
        selectedPipelineId_ = firstAvailablePipelineId_();
        emit selectedPipelineChanged();
    }
    reloadStoredRunRows_();
    reloadStoredVerifyRows_();
    emit selectedSessionChanged();
    emit selectedChannelChanged();
    emit channelChoicesChanged();
    emit channelStatsChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setSelectedChannel(const QString& channel) {
    const QString normalized = channel.trimmed().toLower();
    if (normalized != QStringLiteral("trades") && normalized != QStringLiteral("bookticker") && normalized != QStringLiteral("depth")) return;
    if (selectedChannel_ == normalized) return;
    selectedChannel_ = normalized;
    if (!selectedPipelineAvailable()) selectedPipelineId_ = firstAvailablePipelineId_();
    emit selectedChannelChanged();
    emit selectedPipelineChanged();
    emit runRowsChanged();
    emit verifyRowsChanged();
    emit channelStatsChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setInputFile(const QString& path) {
    manualInputFile_ = path.trimmed();
    selectedSessionId_.clear();
    reloadStoredRunRows_();
    reloadStoredVerifyRows_();
    emit selectedSessionChanged();
    emit channelChoicesChanged();
    emit channelStatsChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setInputFileUrl(const QUrl& url) {
    const QString path = pathFromUrl(url);
    if (!path.isEmpty()) setInputFile(path);
}

void CompressionViewModel::setOutputRoot(const QString& path) {
    manualOutputRoot_ = path.trimmed();
    reloadStoredRunRows_();
    reloadStoredVerifyRows_();
    emit outputRootChanged();
    emit outputRootChoicesChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setOutputRootUrl(const QUrl& url) {
    const QString path = pathFromUrl(url);
    if (!path.isEmpty()) setOutputRoot(path);
}

void CompressionViewModel::setSelectedPipelineId(const QString& pipelineId) {
    if (selectedPipelineId_ == pipelineId) return;
    selectedPipelineId_ = pipelineId;
    emit selectedPipelineChanged();
    emit canRunChanged();
    emit canDecodeVerifyChanged();
    emit artifactAvailabilityChanged();
}

QString CompressionViewModel::existingChannelPath_(const QString& sessionPath, const QString& channel) const {
    if (sessionPath.trimmed().isEmpty()) return {};
    const QString fileName = channelFileName_(channel);
    if (fileName.isEmpty()) return {};
    const QDir dir(sessionPath);
    const QString jsonlPath = dir.absoluteFilePath(QStringLiteral("jsonl/%1").arg(fileName));
    if (QFileInfo::exists(jsonlPath)) return jsonlPath;
    return {};
}

QString CompressionViewModel::preferredChannelPath_(const QString& sessionPath, const QString& channel) const {
    if (sessionPath.trimmed().isEmpty()) return {};
    const QString fileName = channelFileName_(channel);
    return fileName.isEmpty() ? QString{} : QDir(sessionPath).absoluteFilePath(QStringLiteral("jsonl/%1").arg(fileName));
}

QString CompressionViewModel::firstAvailableChannel_(const QString& sessionPath) const {
    const QString channels[] = {QStringLiteral("trades"), QStringLiteral("bookticker"), QStringLiteral("depth")};
    for (const QString& channel : channels) {
        if (!existingChannelPath_(sessionPath, channel).isEmpty()) return channel;
    }
    return {};
}

QString CompressionViewModel::channelFileName_(const QString& channel) const {
    if (channel == QStringLiteral("bookticker")) return QStringLiteral("bookticker.jsonl");
    if (channel == QStringLiteral("depth")) return QStringLiteral("depth_tape.jsonl");
    if (channel == QStringLiteral("trades")) return QStringLiteral("trades.jsonl");
    return {};
}

void CompressionViewModel::emitSelectionChanged_() {
    emit selectionChanged();
    emit inputFileChanged();
    emit outputRootChanged();
    emit outputRootChoicesChanged();
    emit canRunChanged();
    emit canDecodeVerifyChanged();
    emit artifactAvailabilityChanged();
}


}  // namespace hftrec::gui
