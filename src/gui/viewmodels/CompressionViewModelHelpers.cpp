#include "gui/viewmodels/CompressionViewModelHelpers.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonParseError>
#include <QLocale>
#include <QProcess>
#include <QTextStream>

#include "core/recordings/RecordingRoot.hpp"

namespace hftrec::gui::compression_vm {

#ifndef HFT_COMPRESSOR_SOURCE_DIR
#define HFT_COMPRESSOR_SOURCE_DIR "../hft-compressor"
#endif

QString mbps(double value) {
    return QLocale().toString(value, 'f', 2) + QStringLiteral(" MB/s");
}

QString bytesText(std::uint64_t value) {
    return QLocale().formattedDataSize(static_cast<qint64>(value));
}

QString ratioLabel(double value) {
    return QLocale().toString(value, 'f', 3) + QStringLiteral("x");
}

QString percentLabel(double value) {
    return QLocale().toString(value, 'f', 2) + QStringLiteral("%");
}

QString viewString(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

QString variantString(const QVariantMap& map, const QString& key, const QString& fallback) {
    const QString value = map.value(key).toString();
    return value.isEmpty() ? fallback : value;
}

bool variantBool(const QVariantMap& map, const QString& key, bool fallback) {
    return map.contains(key) ? map.value(key).toBool() : fallback;
}

double variantDouble(const QVariantMap& map, const QString& key, double fallback) {
    bool ok = false;
    const double value = map.value(key).toDouble(&ok);
    return ok ? value : fallback;
}

bool isPythonPipelineId(const QString& pipelineId) {
    return pipelineId.startsWith(QStringLiteral("py."));
}

QString pythonCodecDir() {
    return QDir(QStringLiteral(HFT_COMPRESSOR_SOURCE_DIR)).absoluteFilePath(QStringLiteral("src/codecs/python"));
}

QString pythonCodecCliPath() {
    return QDir(pythonCodecDir()).absoluteFilePath(QStringLiteral("cli.py"));
}

QJsonDocument runPythonCodecCli(const QStringList& args, int timeoutMs) {
    QProcess process;
    process.setProgram(QStringLiteral("python3"));
    process.setArguments(QStringList{pythonCodecCliPath()} + args);
    process.setWorkingDirectory(pythonCodecDir());
    process.start();
    if (!process.waitForStarted(5000)) return {};
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    const QByteArray output = process.readAllStandardOutput();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(output, &error);
    return error.error == QJsonParseError::NoError ? document : QJsonDocument{};
}

QVariantList pythonCodecPipelineRows() {
    QVariantList out;
    const QJsonDocument document = runPythonCodecCli(QStringList{QStringLiteral("list")}, 30000);
    if (!document.isArray()) return out;
    for (const auto& value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) continue;
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("label"), object.value(QStringLiteral("label")).toString(id));
        row.insert(QStringLiteral("streamScope"), QStringLiteral("all"));
        row.insert(QStringLiteral("representation"), QStringLiteral("jsonl_bytes"));
        row.insert(QStringLiteral("transform"), QStringLiteral("raw_jsonl"));
        row.insert(QStringLiteral("entropy"), object.value(QStringLiteral("module")).toString(id));
        row.insert(QStringLiteral("profile"), QStringLiteral("research"));
        row.insert(QStringLiteral("profileLabel"), displayProfile(QStringLiteral("research")));
        row.insert(QStringLiteral("implementationKind"), QStringLiteral("python"));
        row.insert(QStringLiteral("group"), QStringLiteral("Python prototypes"));
        const bool available = object.value(QStringLiteral("available")).toBool(false);
        row.insert(QStringLiteral("availability"), available ? QStringLiteral("available") : QStringLiteral("dependency_unavailable"));
        row.insert(QStringLiteral("availabilityReason"), object.value(QStringLiteral("availability_reason")).toString());
        row.insert(QStringLiteral("available"), available);
        row.insert(QStringLiteral("outputSlug"), object.value(QStringLiteral("slug")).toString(id));
        row.insert(QStringLiteral("fileExtension"), object.value(QStringLiteral("extension")).toString());
        row.insert(QStringLiteral("summary"), QStringLiteral("all | jsonl_bytes | raw_jsonl | %1").arg(row.value(QStringLiteral("entropy")).toString()));
        out.push_back(row);
    }
    return out;
}

QVariantMap findPythonPipelineRow(const QString& pipelineId) {
    for (const auto& value : pythonCodecPipelineRows()) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == pipelineId) return row;
    }
    return {};
}

const hft_compressor::PipelineDescriptor* findPipeline(const QString& pipelineId) {
    return hft_compressor::findPipeline(pipelineId.toStdString());
}

hft_compressor::StreamType streamTypeForScope(const QString& scope) {
    if (scope == QStringLiteral("trades")) return hft_compressor::StreamType::Trades;
    if (scope == QStringLiteral("bookticker")) return hft_compressor::StreamType::BookTicker;
    if (scope == QStringLiteral("depth")) return hft_compressor::StreamType::Depth;
    return hft_compressor::StreamType::Unknown;
}

hft_compressor::CompressionResult failedCppCompressionResult(const QVariantMap& row,
                                                             const QString& input,
                                                             const QString& outputPath,
                                                             const QString& error) {
    hft_compressor::CompressionResult result{};
    result.status = hft_compressor::Status::DecodeError;
    result.inputPath = input.toStdString();
    result.outputPath = outputPath.toStdString();
    result.pipelineId = variantString(row, QStringLiteral("id")).toStdString();
    result.representation = variantString(row, QStringLiteral("representation")).toStdString();
    result.transform = variantString(row, QStringLiteral("transform")).toStdString();
    result.entropy = variantString(row, QStringLiteral("entropy")).toStdString();
    result.streamType = streamTypeForScope(variantString(row, QStringLiteral("streamScope")));
    result.error = error.toStdString();
    return result;
}

QString pipelineSummary(const hft_compressor::PipelineDescriptor& pipeline) {
    return QStringLiteral("%1 | %2 | %3 | %4")
        .arg(viewString(pipeline.streamScope),
             viewString(pipeline.representation),
             viewString(pipeline.transform),
             viewString(pipeline.entropy));
}

QString pipelineLabelFor(const QString& pipelineId) {
    const auto* pipeline = findPipeline(pipelineId);
    if (pipeline != nullptr) return viewString(pipeline->label);
    const QVariantMap python = findPythonPipelineRow(pipelineId);
    return python.isEmpty() ? pipelineId : variantString(python, QStringLiteral("label"), pipelineId);
}

QString pipelineFileExtensionFor(const QString& pipelineId) {
    const auto* pipeline = findPipeline(pipelineId);
    if (pipeline != nullptr && !pipeline->fileExtension.empty()) return viewString(pipeline->fileExtension);
    const QVariantMap python = findPythonPipelineRow(pipelineId);
    const QString extension = python.value(QStringLiteral("fileExtension")).toString();
    return extension.isEmpty() ? QStringLiteral(".hfc") : extension;
}

QString pythonOutputSlugFor(const QString& pipelineId) {
    const QVariantMap python = findPythonPipelineRow(pipelineId);
    const QString slug = python.value(QStringLiteral("outputSlug")).toString();
    return slug.isEmpty() ? QString(pipelineId).replace('.', '-') : slug;
}

QString pipelineOutputSlugFor(const QString& pipelineId) {
    const auto* pipeline = findPipeline(pipelineId);
    if (pipeline != nullptr && !pipeline->outputSlug.empty()) return viewString(pipeline->outputSlug);
    const QVariantMap python = findPythonPipelineRow(pipelineId);
    const QString slug = python.value(QStringLiteral("outputSlug")).toString();
    return slug.isEmpty() ? QString(pipelineId).replace('.', '-') : slug;
}

QString sessionIdForInputPath(const QString& inputPath) {
    const QFileInfo inputInfo(inputPath);
    const QDir parent = inputInfo.dir();
    if (parent.dirName() == QStringLiteral("jsonl")) {
        const QFileInfo sessionInfo(parent.absolutePath() + QStringLiteral("/.."));
        if (!sessionInfo.fileName().isEmpty()) return sessionInfo.fileName();
    }
    return parent.dirName().isEmpty() ? QStringLiteral("manual") : parent.dirName();
}

double mbpsValue(std::uint64_t bytes, std::uint64_t ns) {
    if (ns == 0u) return 0.0;
    return (static_cast<double>(bytes) / (1024.0 * 1024.0)) / (static_cast<double>(ns) / 1000000000.0);
}

QString artifactPathFromMetricsPath(const QString& metricsPath, const QString& pipelineId, const QString& metricsSuffix) {
    const QFileInfo info(metricsPath);
    QString stem = info.completeBaseName();
    if (stem.endsWith(metricsSuffix)) stem.chop(metricsSuffix.size());
    return info.absolutePath() + QStringLiteral("/") + stem + pipelineFileExtensionFor(pipelineId);
}

QString pathFromUrl(const QUrl& url) {
    if (!url.isValid() || url.isEmpty()) return QString{};
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    return path.trimmed();
}

QString resolveRecordingsRoot() {
    return QDir::cleanPath(QString::fromStdString(recordings::defaultRecordingsRoot().string()));
}

QString sessionSourceSummary(const BacktestLegCounts& backtestCounts, const QString& sessionPath) {
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return sessionBacktestSummaryText(0, backtestCounts, 0);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject manifest = doc.object();
    const QJsonObject bookTicker = manifest.value(QStringLiteral("channels")).toObject().value(QStringLiteral("bookticker")).toObject();
    const QString summary = sessionBacktestSummaryText(
        bookTicker.value(QStringLiteral("declared_event_count")).toInt(),
        backtestCounts,
        manifest.value(QStringLiteral("capture")).toObject().value(QStringLiteral("started_at_ns")).toInteger());
    return appendSessionHealthSummary(
        summary,
        manifest.value(QStringLiteral("integrity")).toObject().value(QStringLiteral("session_health")).toString(),
        manifest.value(QStringLiteral("summary")).toObject().value(QStringLiteral("warning_summary")).toString());
}

QString displayChannel(const QString& channel) {
    if (channel == QStringLiteral("bookticker")) return QStringLiteral("BookTicker");
    if (channel == QStringLiteral("depth")) return QStringLiteral("Depth");
    return QStringLiteral("Trades");
}

QString displayProfile(const QString& profile) {
    if (profile == QStringLiteral("live")) return QStringLiteral("Live");
    if (profile == QStringLiteral("replay")) return QStringLiteral("Replay");
    if (profile == QStringLiteral("research")) return QStringLiteral("Research");
    return QStringLiteral("Archive");
}

QString normalizedPipelineId(const QJsonObject& object) {
    const QString pipelineId = jsonString(object, "pipeline_id");
    return pipelineId.isEmpty() ? jsonString(object, "codec_id") : pipelineId;
}

QString normalizedInputPath(const QJsonObject& object) {
    const QString path = jsonString(object, "input_path");
    return path.isEmpty() ? jsonString(object, "canonical_path") : path;
}

QString normalizedOutputPath(const QJsonObject& object) {
    const QString path = jsonString(object, "output_path");
    return path.isEmpty() ? jsonString(object, "artifact_path") : path;
}

QString groupForPipeline(const hft_compressor::PipelineDescriptor& pipeline) {
    const QString id = viewString(pipeline.id);
    const QString transform = viewString(pipeline.transform);
    if (id.startsWith(QStringLiteral("std."))) return QStringLiteral("Standard baselines");
    if (id.startsWith(QStringLiteral("py."))) return QStringLiteral("Python prototypes");
    if (id.startsWith(QStringLiteral("custom.")) || viewString(pipeline.entropy).contains(QStringLiteral("rans"))
        || viewString(pipeline.entropy).contains(QStringLiteral("range")) || viewString(pipeline.entropy).contains(QStringLiteral("ac_"))) {
        return QStringLiteral("Custom entropy coders");
    }
    if (transform.contains(QStringLiteral("delta")) || transform.contains(QStringLiteral("columnar"))) {
        return QStringLiteral("Domain transforms");
    }
    return QStringLiteral("Research pipelines");
}

bool isCompressTablePipeline(const hft_compressor::PipelineDescriptor& pipeline) {
    return pipeline.availability != hft_compressor::PipelineAvailability::NotImplemented;
}

bool pipelineMatchesChannel(const hft_compressor::PipelineDescriptor& pipeline, const QString& channel) {
    const QString scope = viewString(pipeline.streamScope);
    return scope == QStringLiteral("all") || scope == channel;
}

std::uint64_t jsonUInt64(const QJsonObject& object, const char* key) {
    const auto value = object.value(QString::fromLatin1(key));
    if (value.isDouble()) return static_cast<std::uint64_t>(value.toDouble());
    if (value.isString()) return value.toString().toULongLong();
    return 0u;
}

QString jsonString(const QJsonObject& object, const char* key) {
    return object.value(QString::fromLatin1(key)).toString();
}

QVariantMap previewError(const QString& path, const QString& error) {
    QVariantMap out;
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("error"), error);
    out.insert(QStringLiteral("summaryText"), error);
    out.insert(QStringLiteral("contentText"), QString{});
    out.insert(QStringLiteral("metadataRows"), QVariantList{});
    out.insert(QStringLiteral("blockRows"), QVariantList{});
    return out;
}

QVariantMap previewOkBase(const QFileInfo& info, const QString& kind) {
    QVariantMap out;
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("kind"), kind);
    out.insert(QStringLiteral("path"), info.absoluteFilePath());
    out.insert(QStringLiteral("fileName"), info.fileName());
    out.insert(QStringLiteral("sizeText"), bytesText(static_cast<std::uint64_t>(info.size())));
    out.insert(QStringLiteral("metadataRows"), QVariantList{});
    out.insert(QStringLiteral("blockRows"), QVariantList{});
    return out;
}

QString printableAscii(char value) {
    const auto code = static_cast<unsigned char>(value);
    return code >= 32u && code <= 126u ? QString(1, QChar(static_cast<char>(code))) : QStringLiteral(".");
}

QString hexDumpText(const QByteArray& bytes) {
    QString out;
    QTextStream stream(&out);
    for (qsizetype offset = 0; offset < bytes.size(); offset += 16) {
        stream << QStringLiteral("%1  ").arg(offset, 8, 16, QLatin1Char('0')).toUpper();
        QString ascii;
        for (qsizetype i = 0; i < 16; ++i) {
            const qsizetype index = offset + i;
            if (index < bytes.size()) {
                const auto byte = static_cast<unsigned char>(bytes[index]);
                stream << QStringLiteral("%1 ").arg(byte, 2, 16, QLatin1Char('0')).toUpper();
                ascii += printableAscii(bytes[index]);
            } else {
                stream << QStringLiteral("   ");
                ascii += QLatin1Char(' ');
            }
            if (i == 7) stream << QLatin1Char(' ');
        }
        stream << QStringLiteral(" |") << ascii << QStringLiteral("|\n");
    }
    return out;
}

QVariantMap metadataRow(const QString& label, const QString& value) {
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    return row;
}

}  // namespace hftrec::gui::compression_vm
