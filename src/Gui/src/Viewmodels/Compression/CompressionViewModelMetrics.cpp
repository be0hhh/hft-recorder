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

QVariantList CompressionViewModel::channelStats() const {
    QVariantList out;
    const QString sessionPath = selectedSessionPath();
    const QString channels[] = {QStringLiteral("trades"), QStringLiteral("bookticker"), QStringLiteral("depth")};
    for (const QString& channel : channels) {
        const QString path = existingChannelPath_(sessionPath, channel);
        QFileInfo info(path);
        QVariantMap row;
        row.insert(QStringLiteral("id"), channel);
        row.insert(QStringLiteral("label"), displayChannel(channel));
        row.insert(QStringLiteral("available"), info.exists());
        row.insert(QStringLiteral("bytes"), static_cast<qulonglong>(info.exists() ? info.size() : 0));
        row.insert(QStringLiteral("sizeText"), bytesText(static_cast<std::uint64_t>(info.exists() ? info.size() : 0)));
        row.insert(QStringLiteral("selected"), selectedChannel_ == channel);
        bool hasMetrics = false;
        for (const auto& value : runRows_) {
            const QVariantMap run = value.toMap();
            if (run.value(QStringLiteral("stream")).toString() == channel) {
                row.insert(QStringLiteral("ratioText"), run.value(QStringLiteral("ratioText")));
                row.insert(QStringLiteral("status"), run.value(QStringLiteral("status")));
                hasMetrics = true;
                break;
            }
        }
        row.insert(QStringLiteral("hasMetrics"), hasMetrics);
        bool hasVerify = false;
        for (const auto& value : verifyRows_) {
            const QVariantMap verify = value.toMap();
            if (verify.value(QStringLiteral("stream")).toString() == channel) {
                row.insert(QStringLiteral("verifyStatus"), verify.value(QStringLiteral("status")));
                row.insert(QStringLiteral("verifyExactText"), verify.value(QStringLiteral("exactText")));
                hasVerify = true;
                break;
            }
        }
        row.insert(QStringLiteral("hasVerify"), hasVerify);
        out.push_back(row);
    }
    return out;
}

QVariantList CompressionViewModel::rowsForSelectedChannel_(const QVariantList& rows) const {
    QVariantList out;
    for (const auto& value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("stream")).toString() == selectedChannel_) out.push_back(row);
    }
    return out;
}

QVariantList CompressionViewModel::compressionBars() const {
    QVariantList bars = runRows();
    std::sort(bars.begin(), bars.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("ratio")).toDouble() > rhs.toMap().value(QStringLiteral("ratio")).toDouble();
    });
    double maxRatio = 1.0;
    for (const auto& value : bars) maxRatio = std::max(maxRatio, value.toMap().value(QStringLiteral("ratio")).toDouble());
    for (auto& value : bars) {
        QVariantMap row = value.toMap();
        const double width = maxRatio <= 0.0 ? 0.0 : (row.value(QStringLiteral("ratio")).toDouble() / maxRatio) * 100.0;
        row.insert(QStringLiteral("barWidth"), width);
        row.insert(QStringLiteral("referenceWidth"), maxRatio <= 0.0 ? 0.0 : (1.0 / maxRatio) * 100.0);
        value = row;
    }
    return bars;
}

QVariantList CompressionViewModel::decodeBars() const {
    QVariantList bars = verifyRows();
    std::sort(bars.begin(), bars.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("decodeMbPerSec")).toDouble() > rhs.toMap().value(QStringLiteral("decodeMbPerSec")).toDouble();
    });
    double maxSpeed = 1.0;
    for (const auto& value : bars) maxSpeed = std::max(maxSpeed, value.toMap().value(QStringLiteral("decodeMbPerSec")).toDouble());
    for (auto& value : bars) {
        QVariantMap row = value.toMap();
        row.insert(QStringLiteral("barWidth"), maxSpeed <= 0.0 ? 0.0 : (row.value(QStringLiteral("decodeMbPerSec")).toDouble() / maxSpeed) * 100.0);
        value = row;
    }
    return bars;
}

QString CompressionViewModel::ratioText() const { return ratioLabel(ratio_); }

QString CompressionViewModel::encodeSpeedText() const { return mbps(encodeMbPerSec_); }

QString CompressionViewModel::decodeSpeedText() const { return mbps(decodeMbPerSec_); }

QString CompressionViewModel::sizeText() const { return bytesText(inputBytes_) + QStringLiteral(" -> ") + bytesText(outputBytes_); }

QString CompressionViewModel::timingText() const {
    return QStringLiteral("encode %1 ns / decode %2 ns / rdtscp %3:%4")
        .arg(encodeNs_)
        .arg(decodeNs_)
        .arg(encodeCycles_)
        .arg(decodeCycles_);
}

QVariantMap CompressionViewModel::resultRow_(const hft_compressor::CompressionResult& result) const {
    QVariantMap row;
    const double currentRatio = hft_compressor::ratio(result);
    const double encode = hft_compressor::encodeMbPerSec(result);
    const double decode = hft_compressor::decodeMbPerSec(result);
    row.insert(QStringLiteral("pipelineId"), QString::fromStdString(result.pipelineId));
    row.insert(QStringLiteral("pipelineLabel"), pipelineLabelFor(QString::fromStdString(result.pipelineId)));
    row.insert(QStringLiteral("stream"), viewString(hft_compressor::streamTypeToString(result.streamType)));
    row.insert(QStringLiteral("streamLabel"), displayChannel(viewString(hft_compressor::streamTypeToString(result.streamType))));
    row.insert(QStringLiteral("profile"), QString::fromStdString(result.profile));
    row.insert(QStringLiteral("profileLabel"), displayProfile(QString::fromStdString(result.profile)));
    row.insert(QStringLiteral("status"), viewString(hft_compressor::statusToString(result.status)));
    row.insert(QStringLiteral("ok"), hft_compressor::isOk(result.status));
    row.insert(QStringLiteral("roundtrip"), result.roundtripOk);
    row.insert(QStringLiteral("inputBytes"), static_cast<qulonglong>(result.inputBytes));
    row.insert(QStringLiteral("outputBytes"), static_cast<qulonglong>(result.outputBytes));
    row.insert(QStringLiteral("lineCount"), static_cast<qulonglong>(result.lineCount));
    row.insert(QStringLiteral("blockCount"), static_cast<qulonglong>(result.blockCount));
    row.insert(QStringLiteral("ratio"), currentRatio);
    row.insert(QStringLiteral("ratioText"), ratioLabel(currentRatio));
    row.insert(QStringLiteral("spaceSavedText"), result.inputBytes == 0u ? QStringLiteral("0%") : QLocale().toString((1.0 - (static_cast<double>(result.outputBytes) / static_cast<double>(result.inputBytes))) * 100.0, 'f', 1) + QStringLiteral("%"));
    row.insert(QStringLiteral("encodeMbPerSec"), encode);
    row.insert(QStringLiteral("decodeMbPerSec"), decode);
    row.insert(QStringLiteral("encodeText"), mbps(encode));
    row.insert(QStringLiteral("decodeText"), mbps(decode));
    row.insert(QStringLiteral("sizeText"), bytesText(result.inputBytes) + QStringLiteral(" -> ") + bytesText(result.outputBytes));
    row.insert(QStringLiteral("outputFile"), QString::fromStdString(result.outputPath.string()));
    row.insert(QStringLiteral("metricsFile"), QString::fromStdString(result.metricsPath.string()));
    row.insert(QStringLiteral("source"), QStringLiteral("run"));
    return row;
}

QVariantMap CompressionViewModel::verifyRow_(const hft_compressor::DecodeVerifyResult& result) const {
    QVariantMap row;
    const QString stream = viewString(hft_compressor::streamTypeToString(result.streamType));
    const double decode = hft_compressor::decodeMbPerSec(result);
    row.insert(QStringLiteral("pipelineId"), QString::fromStdString(result.pipelineId));
    row.insert(QStringLiteral("pipelineLabel"), pipelineLabelFor(QString::fromStdString(result.pipelineId)));
    row.insert(QStringLiteral("stream"), stream);
    row.insert(QStringLiteral("streamLabel"), displayChannel(stream));
    row.insert(QStringLiteral("profile"), QString::fromStdString(result.profile));
    row.insert(QStringLiteral("profileLabel"), displayProfile(QString::fromStdString(result.profile)));
    row.insert(QStringLiteral("status"), viewString(hft_compressor::statusToString(result.status)));
    row.insert(QStringLiteral("ok"), hft_compressor::isOk(result.status));
    row.insert(QStringLiteral("verified"), result.verified);
    row.insert(QStringLiteral("exactText"), result.verified ? QStringLiteral("exact") : QStringLiteral("mismatch"));
    row.insert(QStringLiteral("compressedBytes"), static_cast<qulonglong>(result.compressedBytes));
    row.insert(QStringLiteral("decodedBytes"), static_cast<qulonglong>(result.decodedBytes));
    row.insert(QStringLiteral("canonicalBytes"), static_cast<qulonglong>(result.canonicalBytes));
    row.insert(QStringLiteral("comparedBytes"), static_cast<qulonglong>(result.comparedBytes));
    row.insert(QStringLiteral("mismatchBytes"), static_cast<qulonglong>(result.mismatchBytes));
    row.insert(QStringLiteral("mismatchPercent"), result.mismatchPercent);
    row.insert(QStringLiteral("mismatchPercentText"), percentLabel(result.mismatchPercent));
    row.insert(QStringLiteral("lineCount"), static_cast<qulonglong>(result.lineCount));
    row.insert(QStringLiteral("blockCount"), static_cast<qulonglong>(result.blockCount));
    row.insert(QStringLiteral("decodeMbPerSec"), decode);
    row.insert(QStringLiteral("decodeText"), mbps(decode));
    row.insert(QStringLiteral("compressedSizeText"), bytesText(result.compressedBytes));
    row.insert(QStringLiteral("decodedSizeText"), bytesText(result.decodedBytes));
    row.insert(QStringLiteral("canonicalSizeText"), bytesText(result.canonicalBytes));
    row.insert(QStringLiteral("sizeText"), bytesText(result.decodedBytes) + QStringLiteral(" / эталон ") + bytesText(result.canonicalBytes));
    row.insert(QStringLiteral("mismatchOffset"), static_cast<qulonglong>(result.firstMismatchOffset));
    row.insert(QStringLiteral("mismatchText"), result.verified ? QStringLiteral("совпадает") : QStringLiteral("не совпало на %1").arg(percentLabel(result.mismatchPercent)));
    row.insert(QStringLiteral("compressedFile"), QString::fromStdString(result.compressedPath.string()));
    row.insert(QStringLiteral("canonicalFile"), QString::fromStdString(result.canonicalPath.string()));
    row.insert(QStringLiteral("metricsFile"), QString::fromStdString(result.metricsPath.string()));
    row.insert(QStringLiteral("source"), QStringLiteral("run"));
    return row;
}

QVariantMap CompressionViewModel::metricsRow_(const QString& metricsPath) const {
    QFile file(metricsPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return {};
    const QJsonObject object = document.object();
    const QString stream = jsonString(object, "stream");
    const QString pipelineId = normalizedPipelineId(object);
    if (stream.isEmpty() || pipelineId.isEmpty()) return {};
    if (findPipeline(pipelineId) == nullptr && findPythonPipelineRow(pipelineId).isEmpty()) return {};
    const auto inputBytes = jsonUInt64(object, "input_bytes");
    const auto outputBytes = jsonUInt64(object, "output_bytes");
    const auto encodeNs = jsonUInt64(object, "encode_ns");
    const auto decodeNs = jsonUInt64(object, "decode_ns");
    double currentRatio = object.value(QStringLiteral("compression_ratio")).toDouble();
    if (currentRatio <= 0.0) currentRatio = object.value(QStringLiteral("ratio")).toDouble();
    if (currentRatio <= 0.0 && outputBytes != 0u) currentRatio = static_cast<double>(inputBytes) / static_cast<double>(outputBytes);
    const double encode = object.contains(QStringLiteral("encode_mb_per_sec"))
        ? object.value(QStringLiteral("encode_mb_per_sec")).toDouble()
        : mbpsValue(inputBytes, encodeNs);
    const double decode = object.contains(QStringLiteral("decode_mb_per_sec"))
        ? object.value(QStringLiteral("decode_mb_per_sec")).toDouble()
        : mbpsValue(inputBytes, decodeNs);
    QVariantMap row;
    row.insert(QStringLiteral("pipelineId"), pipelineId);
    row.insert(QStringLiteral("pipelineLabel"), pipelineLabelFor(pipelineId));
    row.insert(QStringLiteral("stream"), stream);
    row.insert(QStringLiteral("streamLabel"), displayChannel(stream));
    const QString profile = jsonString(object, "profile").isEmpty() ? QStringLiteral("research") : jsonString(object, "profile");
    row.insert(QStringLiteral("profile"), profile);
    row.insert(QStringLiteral("profileLabel"), displayProfile(profile));
    row.insert(QStringLiteral("status"), jsonString(object, "status"));
    row.insert(QStringLiteral("ok"), jsonString(object, "status") == QStringLiteral("ok"));
    row.insert(QStringLiteral("roundtrip"), object.value(QStringLiteral("roundtrip_ok")).toBool());
    row.insert(QStringLiteral("inputBytes"), static_cast<qulonglong>(inputBytes));
    row.insert(QStringLiteral("outputBytes"), static_cast<qulonglong>(outputBytes));
    row.insert(QStringLiteral("lineCount"), static_cast<qulonglong>(jsonUInt64(object, "line_count")));
    row.insert(QStringLiteral("blockCount"), static_cast<qulonglong>(jsonUInt64(object, "block_count")));
    row.insert(QStringLiteral("ratio"), currentRatio);
    row.insert(QStringLiteral("ratioText"), ratioLabel(currentRatio));
    row.insert(QStringLiteral("spaceSavedText"), inputBytes == 0u ? QStringLiteral("0%") : QLocale().toString((1.0 - (static_cast<double>(outputBytes) / static_cast<double>(inputBytes))) * 100.0, 'f', 1) + QStringLiteral("%"));
    row.insert(QStringLiteral("encodeMbPerSec"), encode);
    row.insert(QStringLiteral("decodeMbPerSec"), decode);
    row.insert(QStringLiteral("encodeText"), mbps(encode));
    row.insert(QStringLiteral("decodeText"), mbps(decode));
    row.insert(QStringLiteral("sizeText"), bytesText(inputBytes) + QStringLiteral(" -> ") + bytesText(outputBytes));
    const QString outputPath = normalizedOutputPath(object);
    row.insert(QStringLiteral("outputFile"), outputPath.isEmpty() ? artifactPathFromMetricsPath(metricsPath, pipelineId, QStringLiteral(".metrics")) : outputPath);
    row.insert(QStringLiteral("metricsFile"), metricsPath);
    row.insert(QStringLiteral("source"), QStringLiteral("stored"));
    return row;
}

QVariantMap CompressionViewModel::verifyMetricsRow_(const QString& metricsPath) const {
    QFile file(metricsPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return {};
    const QJsonObject object = document.object();
    const QString stream = jsonString(object, "stream");
    const QString pipelineId = jsonString(object, "pipeline_id");
    if (stream.isEmpty() || pipelineId.isEmpty()) return {};
    if (findPipeline(pipelineId) == nullptr && findPythonPipelineRow(pipelineId).isEmpty()) return {};
    const auto decodedBytes = jsonUInt64(object, "decoded_bytes");
    const auto canonicalBytes = jsonUInt64(object, "canonical_bytes");
    const double decode = object.value(QStringLiteral("decode_mb_per_sec")).toDouble();
    const double mismatchPercent = object.value(QStringLiteral("mismatch_percent")).toDouble();
    const bool verified = object.value(QStringLiteral("verified")).toBool();
    QVariantMap row;
    row.insert(QStringLiteral("pipelineId"), pipelineId);
    row.insert(QStringLiteral("pipelineLabel"), pipelineLabelFor(pipelineId));
    row.insert(QStringLiteral("stream"), stream);
    row.insert(QStringLiteral("streamLabel"), displayChannel(stream));
    row.insert(QStringLiteral("profile"), jsonString(object, "profile"));
    row.insert(QStringLiteral("profileLabel"), displayProfile(jsonString(object, "profile")));
    row.insert(QStringLiteral("status"), jsonString(object, "status"));
    row.insert(QStringLiteral("ok"), jsonString(object, "status") == QStringLiteral("ok"));
    row.insert(QStringLiteral("verified"), verified);
    row.insert(QStringLiteral("exactText"), verified ? QStringLiteral("exact") : QStringLiteral("mismatch"));
    row.insert(QStringLiteral("compressedBytes"), static_cast<qulonglong>(jsonUInt64(object, "compressed_bytes")));
    row.insert(QStringLiteral("decodedBytes"), static_cast<qulonglong>(decodedBytes));
    row.insert(QStringLiteral("canonicalBytes"), static_cast<qulonglong>(canonicalBytes));
    row.insert(QStringLiteral("comparedBytes"), static_cast<qulonglong>(jsonUInt64(object, "compared_bytes")));
    row.insert(QStringLiteral("mismatchBytes"), static_cast<qulonglong>(jsonUInt64(object, "mismatch_bytes")));
    row.insert(QStringLiteral("mismatchPercent"), mismatchPercent);
    row.insert(QStringLiteral("mismatchPercentText"), percentLabel(mismatchPercent));
    row.insert(QStringLiteral("lineCount"), static_cast<qulonglong>(jsonUInt64(object, "line_count")));
    row.insert(QStringLiteral("blockCount"), static_cast<qulonglong>(jsonUInt64(object, "block_count")));
    row.insert(QStringLiteral("decodeMbPerSec"), decode);
    row.insert(QStringLiteral("decodeText"), mbps(decode));
    row.insert(QStringLiteral("compressedSizeText"), bytesText(jsonUInt64(object, "compressed_bytes")));
    row.insert(QStringLiteral("decodedSizeText"), bytesText(decodedBytes));
    row.insert(QStringLiteral("canonicalSizeText"), bytesText(canonicalBytes));
    row.insert(QStringLiteral("sizeText"), bytesText(decodedBytes) + QStringLiteral(" / эталон ") + bytesText(canonicalBytes));
    row.insert(QStringLiteral("mismatchOffset"), static_cast<qulonglong>(jsonUInt64(object, "first_mismatch_offset")));
    row.insert(QStringLiteral("mismatchText"), verified ? QStringLiteral("совпадает") : QStringLiteral("не совпало на %1").arg(percentLabel(mismatchPercent)));
    row.insert(QStringLiteral("compressedFile"), artifactPathFromMetricsPath(metricsPath, pipelineId, QStringLiteral(".verify")));
    row.insert(QStringLiteral("metricsFile"), metricsPath);
    row.insert(QStringLiteral("source"), QStringLiteral("stored"));
    return row;
}


}  // namespace hftrec::gui
