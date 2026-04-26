#include "gui/viewmodels/CompressionViewModel.hpp"

#include <QLocale>
#include <QVariantMap>

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

}  // namespace

CompressionViewModel::CompressionViewModel(QObject* parent)
    : QObject(parent), outputRoot_(QString::fromStdString(hft_compressor::defaultOutputRoot().string())) {
    const auto allPipelines = hft_compressor::listPipelines();
    if (!allPipelines.empty()) selectedPipelineId_ = viewString(allPipelines.front().id);
    compressionMetricsServer_ = std::make_unique<hft_compressor::MetricsServer>();
    compressionMetricsServer_->startFromEnvironment();
}

CompressionViewModel::~CompressionViewModel() = default;

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
    const auto stream = hft_compressor::inferStreamTypeFromPath(inputFile_.toStdString());
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
    return selectedPipelineAvailable()
        && hft_compressor::inferStreamTypeFromPath(inputFile_.toStdString()) != hft_compressor::StreamType::Unknown;
}

void CompressionViewModel::setInputFile(const QString& path) {
    if (inputFile_ == path) return;
    inputFile_ = path;
    emit inputFileChanged();
    emit canRunChanged();
}

void CompressionViewModel::setInputFileUrl(const QUrl& url) {
    setInputFile(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void CompressionViewModel::setOutputRoot(const QString& path) {
    if (outputRoot_ == path) return;
    outputRoot_ = path;
    emit outputRootChanged();
}

void CompressionViewModel::setOutputRootUrl(const QUrl& url) {
    setOutputRoot(url.isLocalFile() ? url.toLocalFile() : url.toString());
}

void CompressionViewModel::setSelectedPipelineId(const QString& pipelineId) {
    if (selectedPipelineId_ == pipelineId) return;
    selectedPipelineId_ = pipelineId;
    emit selectedPipelineChanged();
    emit canRunChanged();
}

void CompressionViewModel::runCompression() {
    hft_compressor::CompressionRequest request{};
    request.inputPath = inputFile_.toStdString();
    request.outputRoot = outputRoot_.toStdString();
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
}

}  // namespace hftrec::gui
