#include "gui/viewmodels/CompressionViewModel.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QVariantMap>

#include <filesystem>

#include "hft_compressor/compressor.hpp"
#include "hft_compressor/metrics_server.hpp"

namespace hftrec::gui {
namespace {

QString mbps(double value) {
    return QLocale().toString(value, 'f', 2) + QStringLiteral(" MB/s");
}

QString bytesText(std::uint64_t value) {
    return QLocale().formattedDataSize(static_cast<qint64>(value));
}

QString viewString(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

const hft_compressor::PipelineDescriptor* findPipeline(const QString& pipelineId) {
    return hft_compressor::findPipeline(pipelineId.toStdString());
}

QString pipelineSummary(const hft_compressor::PipelineDescriptor& pipeline) {
    return QStringLiteral("%1 | %2 | %3 | %4")
        .arg(viewString(pipeline.streamScope),
             viewString(pipeline.representation),
             viewString(pipeline.transform),
             viewString(pipeline.entropy));
}

QString pathFromUrl(const QUrl& url) {
    if (!url.isValid() || url.isEmpty()) return QString{};
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    return path.trimmed();
}

QString resolveRecordingsRoot() {
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
}

QString displayChannel(const QString& channel) {
    if (channel == QStringLiteral("bookticker")) return QStringLiteral("BookTicker");
    if (channel == QStringLiteral("depth")) return QStringLiteral("Depth");
    return QStringLiteral("Trades");
}

}  // namespace

CompressionViewModel::CompressionViewModel(QObject* parent)
    : QObject(parent) {
    const auto allPipelines = hft_compressor::listPipelines();
    if (!allPipelines.empty()) selectedPipelineId_ = viewString(allPipelines.front().id);

    const auto sessionRows = sessions();
    if (!sessionRows.empty()) {
        selectedSessionId_ = sessionRows.front().toMap().value(QStringLiteral("id")).toString();
        const QString firstChannel = firstAvailableChannel_(selectedSessionPath());
        if (!firstChannel.isEmpty()) selectedChannel_ = firstChannel;
    }

    compressionMetricsServer_ = std::make_unique<hft_compressor::MetricsServer>();
    compressionMetricsServer_->startFromEnvironment();
}

CompressionViewModel::~CompressionViewModel() = default;

QString CompressionViewModel::recordingsRoot() const {
    return resolveRecordingsRoot();
}

QVariantList CompressionViewModel::sessions() const {
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
        row.insert(QStringLiteral("hasTrades"), !existingChannelPath_(path, QStringLiteral("trades")).isEmpty());
        row.insert(QStringLiteral("hasBookTicker"), !existingChannelPath_(path, QStringLiteral("bookticker")).isEmpty());
        row.insert(QStringLiteral("hasDepth"), !existingChannelPath_(path, QStringLiteral("depth")).isEmpty());
        out.push_back(row);
    }
    return out;
}

QString CompressionViewModel::selectedSessionPath() const {
    if (selectedSessionId_.trimmed().isEmpty()) return {};
    return QDir(recordingsRoot()).absoluteFilePath(selectedSessionId_);
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

QString CompressionViewModel::inputFile() const {
    if (!manualInputFile_.trimmed().isEmpty() && selectedSessionId_.trimmed().isEmpty()) return manualInputFile_;
    const QString path = existingChannelPath_(selectedSessionPath(), selectedChannel_);
    return path.isEmpty() ? preferredChannelPath_(selectedSessionPath(), selectedChannel_) : path;
}

QString CompressionViewModel::outputRoot() const {
    if (!manualOutputRoot_.trimmed().isEmpty() && selectedSessionId_.trimmed().isEmpty()) return manualOutputRoot_;
    const QString sessionPath = selectedSessionPath();
    if (sessionPath.isEmpty()) return {};
    return QDir(sessionPath).absoluteFilePath(QStringLiteral("compressed/zstd"));
}

QString CompressionViewModel::outputFilePreview() const {
    const QString root = outputRoot();
    const QString fileName = channelFileName_(selectedChannel_).replace(QStringLiteral(".jsonl"), QStringLiteral(".hfc"));
    if (root.isEmpty() || fileName.isEmpty()) return {};
    return QDir(root).absoluteFilePath(fileName);
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
        QVariantMap row;
        row.insert(QStringLiteral("id"), viewString(pipeline.id));
        row.insert(QStringLiteral("label"), viewString(pipeline.label));
        row.insert(QStringLiteral("streamScope"), viewString(pipeline.streamScope));
        row.insert(QStringLiteral("representation"), viewString(pipeline.representation));
        row.insert(QStringLiteral("transform"), viewString(pipeline.transform));
        row.insert(QStringLiteral("entropy"), viewString(pipeline.entropy));
        row.insert(QStringLiteral("profile"), viewString(pipeline.profile));
        row.insert(QStringLiteral("implementationKind"), viewString(pipeline.implementationKind));
        row.insert(QStringLiteral("availability"), viewString(hft_compressor::pipelineAvailabilityToString(pipeline.availability)));
        row.insert(QStringLiteral("availabilityReason"), viewString(pipeline.availabilityReason));
        row.insert(QStringLiteral("available"), pipeline.availability == hft_compressor::PipelineAvailability::Available);
        row.insert(QStringLiteral("summary"), pipelineSummary(pipeline));
        out.push_back(row);
    }
    return out;
}

QString CompressionViewModel::selectedPipelineLabel() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    return pipeline == nullptr ? selectedPipelineId_ : viewString(pipeline->label);
}

QString CompressionViewModel::selectedPipelineSummary() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    return pipeline == nullptr ? QString{} : pipelineSummary(*pipeline);
}

bool CompressionViewModel::selectedPipelineAvailable() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    return pipeline != nullptr && pipeline->availability == hft_compressor::PipelineAvailability::Available;
}

QString CompressionViewModel::inferredStream() const {
    const auto stream = hft_compressor::inferStreamTypeFromPath(inputFile().toStdString());
    return viewString(hft_compressor::streamTypeToString(stream));
}

QString CompressionViewModel::outputFile() const {
    return outputFile_;
}

QString CompressionViewModel::metricsFile() const {
    return metricsFile_;
}

QString CompressionViewModel::ratioText() const {
    return QLocale().toString(ratio_, 'f', 3) + QStringLiteral("x");
}

QString CompressionViewModel::encodeSpeedText() const {
    return mbps(encodeMbPerSec_);
}

QString CompressionViewModel::decodeSpeedText() const {
    return mbps(decodeMbPerSec_);
}

QString CompressionViewModel::sizeText() const {
    return bytesText(inputBytes_) + QStringLiteral(" -> ") + bytesText(outputBytes_);
}

QString CompressionViewModel::timingText() const {
    return QStringLiteral("encode %1 ns / decode %2 ns / rdtscp %3:%4")
        .arg(encodeNs_)
        .arg(decodeNs_)
        .arg(encodeCycles_)
        .arg(decodeCycles_);
}

bool CompressionViewModel::canRun() const {
    const QString path = inputFile();
    return selectedPipelineAvailable()
        && !path.isEmpty()
        && QFileInfo::exists(path)
        && hft_compressor::inferStreamTypeFromPath(path.toStdString()) != hft_compressor::StreamType::Unknown;
}

void CompressionViewModel::reloadSessions() {
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
    emit sessionsChanged();
    emit channelChoicesChanged();
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
    emit selectedSessionChanged();
    emit selectedChannelChanged();
    emit channelChoicesChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setSelectedChannel(const QString& channel) {
    const QString normalized = channel.trimmed().toLower();
    if (normalized != QStringLiteral("trades")
        && normalized != QStringLiteral("bookticker")
        && normalized != QStringLiteral("depth")) {
        return;
    }
    if (selectedChannel_ == normalized) return;
    selectedChannel_ = normalized;
    emit selectedChannelChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setInputFile(const QString& path) {
    manualInputFile_ = path.trimmed();
    selectedSessionId_.clear();
    emit selectedSessionChanged();
    emit channelChoicesChanged();
    emitSelectionChanged_();
}

void CompressionViewModel::setInputFileUrl(const QUrl& url) {
    const QString path = pathFromUrl(url);
    if (!path.isEmpty()) setInputFile(path);
}

void CompressionViewModel::setOutputRoot(const QString& path) {
    manualOutputRoot_ = path.trimmed();
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
}

void CompressionViewModel::runCompression() {
    hft_compressor::CompressionRequest request{};
    request.inputPath = inputFile().toStdString();
    request.outputPathOverride = outputFilePreview().toStdString();
    request.pipelineId = selectedPipelineId_.toStdString();
    const auto result = hft_compressor::compress(request);
    outputFile_ = QString::fromStdString(result.outputPath.string());
    metricsFile_ = QString::fromStdString(result.metricsPath.string());
    resultPipelineText_ = QStringLiteral("%1 | %2 | %3 | %4")
        .arg(QString::fromStdString(result.pipelineId),
             QString::fromStdString(result.representation),
             QString::fromStdString(result.transform),
             QString::fromStdString(result.entropy));
    inputBytes_ = result.inputBytes;
    outputBytes_ = result.outputBytes;
    encodeNs_ = result.encodeNs;
    decodeNs_ = result.decodeNs;
    encodeCycles_ = result.encodeCycles;
    decodeCycles_ = result.decodeCycles;
    ratio_ = hft_compressor::ratio(result);
    encodeMbPerSec_ = hft_compressor::encodeMbPerSec(result);
    decodeMbPerSec_ = hft_compressor::decodeMbPerSec(result);
    statusText_ = hft_compressor::isOk(result.status)
        ? QStringLiteral("Compression complete")
        : QStringLiteral("Compression failed: ") + QString::fromStdString(result.error);
    emit resultChanged();
    emit canRunChanged();
}

QString CompressionViewModel::existingChannelPath_(const QString& sessionPath, const QString& channel) const {
    if (sessionPath.trimmed().isEmpty()) return {};
    const QString fileName = channelFileName_(channel);
    if (fileName.isEmpty()) return {};
    const QDir dir(sessionPath);
    const QString jsonlPath = dir.absoluteFilePath(QStringLiteral("jsonl/%1").arg(fileName));
    if (QFileInfo::exists(jsonlPath)) return jsonlPath;
    const QString legacyPath = dir.absoluteFilePath(fileName);
    if (QFileInfo::exists(legacyPath)) return legacyPath;
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
    if (channel == QStringLiteral("depth")) return QStringLiteral("depth.jsonl");
    if (channel == QStringLiteral("trades")) return QStringLiteral("trades.jsonl");
    return {};
}

void CompressionViewModel::emitSelectionChanged_() {
    emit selectionChanged();
    emit inputFileChanged();
    emit outputRootChanged();
    emit outputRootChoicesChanged();
    emit canRunChanged();
}

}  // namespace hftrec::gui