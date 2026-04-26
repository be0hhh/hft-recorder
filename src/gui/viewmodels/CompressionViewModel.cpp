#include "gui/viewmodels/CompressionViewModel.hpp"

#include <QLocale>

#include "hft_compressor/compressor.hpp"

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

}  // namespace

CompressionViewModel::CompressionViewModel(QObject* parent)
    : QObject(parent), outputRoot_(QString::fromStdString(hft_compressor::defaultOutputRoot().string())) {}

QString CompressionViewModel::inferredStream() const {
    const auto stream = hft_compressor::inferStreamTypeFromPath(inputFile_.toStdString());
    return viewString(hft_compressor::streamTypeToString(stream));
}

QString CompressionViewModel::outputFile() const {
    return QString::fromStdString(lastResult_.outputPath.string());
}

QString CompressionViewModel::metricsFile() const {
    return QString::fromStdString(lastResult_.metricsPath.string());
}

QString CompressionViewModel::ratioText() const {
    return QLocale().toString(hft_compressor::ratio(lastResult_), 'f', 3) + QStringLiteral("x");
}

QString CompressionViewModel::encodeSpeedText() const {
    return mbps(hft_compressor::encodeMbPerSec(lastResult_));
}

QString CompressionViewModel::decodeSpeedText() const {
    return mbps(hft_compressor::decodeMbPerSec(lastResult_));
}

QString CompressionViewModel::sizeText() const {
    return bytesText(lastResult_.inputBytes) + QStringLiteral(" -> ") + bytesText(lastResult_.outputBytes);
}

QString CompressionViewModel::timingText() const {
    return QStringLiteral("encode %1 ns / decode %2 ns / rdtscp %3:%4")
        .arg(lastResult_.encodeNs)
        .arg(lastResult_.decodeNs)
        .arg(lastResult_.encodeCycles)
        .arg(lastResult_.decodeCycles);
}

bool CompressionViewModel::canRun() const {
    return hft_compressor::inferStreamTypeFromPath(inputFile_.toStdString()) != hft_compressor::StreamType::Unknown;
}

void CompressionViewModel::setInputFile(const QString& path) {
    if (inputFile_ == path) return;
    inputFile_ = path;
    emit inputFileChanged();
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

void CompressionViewModel::runCompression() {
    hft_compressor::CompressionRequest request{};
    request.inputPath = inputFile_.toStdString();
    request.outputRoot = outputRoot_.toStdString();
    lastResult_ = hft_compressor::compressFile(request);
    statusText_ = hft_compressor::isOk(lastResult_.status)
        ? QStringLiteral("Compression complete")
        : QStringLiteral("Compression failed: ") + QString::fromStdString(lastResult_.error);
    emit resultChanged();
}

}  // namespace hftrec::gui