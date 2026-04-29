#include "gui/viewmodels/CompressionViewModel.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QMetaObject>
#include <QPointer>
#include <QTextStream>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <filesystem>
#include <thread>
#include <vector>

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

QString ratioLabel(double value) {
    return QLocale().toString(value, 'f', 3) + QStringLiteral("x");
}

QString percentLabel(double value) {
    return QLocale().toString(value, 'f', 2) + QStringLiteral("%");
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
QString pipelineLabelFor(const QString& pipelineId) {
    const auto* pipeline = findPipeline(pipelineId);
    return pipeline == nullptr ? pipelineId : viewString(pipeline->label);
}

QString pipelineFileExtensionFor(const QString& pipelineId) {
    const auto* pipeline = findPipeline(pipelineId);
    return pipeline == nullptr || pipeline->fileExtension.empty()
        ? QStringLiteral(".hfc")
        : viewString(pipeline->fileExtension);
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
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString candidate = appDir.absoluteFilePath(QStringLiteral("../../recordings"));
    return QDir::cleanPath(QFileInfo(candidate).absoluteFilePath());
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

}  // namespace

CompressionViewModel::CompressionViewModel(QObject* parent)
    : QObject(parent) {
    const auto sessionRows = sessions();
    if (!sessionRows.empty()) {
        selectedSessionId_ = sessionRows.front().toMap().value(QStringLiteral("id")).toString();
        const QString firstChannel = firstAvailableChannel_(selectedSessionPath());
        if (!firstChannel.isEmpty()) selectedChannel_ = firstChannel;
    }
    selectedPipelineId_ = firstAvailablePipelineId_();
    reloadStoredRunRows_();
    reloadStoredVerifyRows_();

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

bool CompressionViewModel::hasSessions() const {
    return !sessions().empty();
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

QString CompressionViewModel::firstAvailablePipelineId_() const {
    for (const auto& pipeline : hft_compressor::listPipelines()) {
        if (pipeline.availability != hft_compressor::PipelineAvailability::Available) continue;
        const QString scope = viewString(pipeline.streamScope);
        if (scope == QStringLiteral("all") || scope == selectedChannel_) return viewString(pipeline.id);
    }
    return {};
}

QVariantList CompressionViewModel::runRows() const {
    return rowsForSelectedChannel_(runRows_);
}

QVariantList CompressionViewModel::verifyRows() const {
    return rowsForSelectedChannel_(verifyRows_);
}

QVariantList CompressionViewModel::speedSeries() const {
    return runRows();
}

QVariantList CompressionViewModel::decodeSpeedSeries() const {
    return verifyRows();
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
    return outputFilePreviewFor_(selectedChannel_);
}

QString CompressionViewModel::selectedArtifactFile() const {
    const QString encoded = encodedArtifactPath_(selectedChannel_, selectedPipelineId_);
    return encoded.isEmpty() ? outputFilePreview() : encoded;
}

bool CompressionViewModel::selectedArtifactAvailable() const {
    return encodedArtifactExists_(selectedChannel_, selectedPipelineId_);
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
        if (pipeline.availability != hft_compressor::PipelineAvailability::Available) continue;
        const QString scope = viewString(pipeline.streamScope);
        if (scope != QStringLiteral("all") && scope != selectedChannel_) continue;
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
            if (groupForPipeline(pipeline) != name) continue;
            ++total;
            if (pipeline.availability == hft_compressor::PipelineAvailability::Available) ++available;
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

QString CompressionViewModel::emptyStateText() const {
    if (!hasSessions()) return QStringLiteral("Нет записанных сессий. Сначала запиши данные во вкладке Capture.");
    if (selectedSessionId_.trimmed().isEmpty()) return QStringLiteral("Выбери сессию, чтобы открыть статистику сжатия.");
    if (firstAvailableChannel_(selectedSessionPath()).isEmpty()) return QStringLiteral("В сессии нет JSONL файлов для сжатия.");
    return QString{};
}

QString CompressionViewModel::metricsEndpointText() const {
    bool ok = false;
    const int envPort = qEnvironmentVariableIntValue("HFT_COMPRESSOR_METRICS_PORT", &ok);
    const int port = ok && envPort > 0 ? envPort : 8081;
    return QStringLiteral("Prometheus: http://127.0.0.1:%1/metrics").arg(port);
}

QString CompressionViewModel::selectedPipelineLabel() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    return pipelineLabelFor(selectedPipelineId_);
}

QString CompressionViewModel::selectedPipelineSummary() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    return pipeline == nullptr ? QString{} : pipelineSummary(*pipeline);
}

bool CompressionViewModel::selectedPipelineAvailable() const {
    const auto* pipeline = findPipeline(selectedPipelineId_);
    if (pipeline == nullptr || pipeline->availability != hft_compressor::PipelineAvailability::Available) return false;
    const QString scope = viewString(pipeline->streamScope);
    return scope == QStringLiteral("all") || scope == selectedChannel_;
}

QString CompressionViewModel::inferredStream() const {
    const auto stream = hft_compressor::inferStreamTypeFromPath(inputFile().toStdString());
    return viewString(hft_compressor::streamTypeToString(stream));
}

QString CompressionViewModel::outputFile() const { return outputFile_; }
QString CompressionViewModel::metricsFile() const { return metricsFile_; }
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

QString CompressionViewModel::verifySpeedText() const { return mbps(verifyDecodeMbPerSec_); }

QString CompressionViewModel::verifyExactText() const {
    if (verifyFile_.isEmpty()) return QStringLiteral("not checked");
    return verifyExact_ ? QStringLiteral("exact match") : QStringLiteral("mismatch");
}

QString CompressionViewModel::verifyMismatchText() const {
    if (verifyFile_.isEmpty()) return QStringLiteral("проверка еще не запускалась");
    if (verifyExact_) return QStringLiteral("декодированные байты совпадают с эталонным JSONL");
    return QStringLiteral("первое расхождение на байте %1").arg(verifyMismatchOffset_);
}

bool CompressionViewModel::canRun() const {
    const QString path = inputFile();
    return !running_
        && !verifying_
        && selectedPipelineAvailable()
        && !path.isEmpty()
        && QFileInfo::exists(path)
        && hft_compressor::inferStreamTypeFromPath(path.toStdString()) != hft_compressor::StreamType::Unknown;
}

bool CompressionViewModel::canDecodeVerify() const {
    return !verifying_
        && !running_
        && selectedPipelineAvailable()
        && QFileInfo::exists(inputFile())
        && encodedArtifactExists_(selectedChannel_, selectedPipelineId_);
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

bool CompressionViewModel::hasEncodedArtifact(const QString& pipelineId) const {
    return encodedArtifactExists_(selectedChannel_, pipelineId.trimmed());
}

QString CompressionViewModel::firstEncodedPipelineId() const {
    for (const auto& pipeline : hft_compressor::listPipelines()) {
        if (pipeline.availability != hft_compressor::PipelineAvailability::Available) continue;
        const QString scope = viewString(pipeline.streamScope);
        if (scope != QStringLiteral("all") && scope != selectedChannel_) continue;
        const QString id = viewString(pipeline.id);
        if (encodedArtifactExists_(selectedChannel_, id)) return id;
    }
    return {};
}

QVariantMap CompressionViewModel::previewJsonl(const QString& path) const {
    const QString previewPath = path.trimmed().isEmpty() ? inputFile() : path.trimmed();
    QFileInfo info(previewPath);
    if (previewPath.isEmpty()) return previewError(previewPath, QStringLiteral("JSONL file is not selected"));
    if (!info.exists()) return previewError(previewPath, QStringLiteral("JSONL file is not found"));

    QFile file(previewPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return previewError(previewPath, QStringLiteral("failed to open JSONL file"));
    }

    constexpr int kMaxLines = 200;
    constexpr qint64 kMaxBytes = 256 * 1024;
    QVariantList rows;
    QString content;
    QTextStream stream(&content);
    int lineNo = 0;
    qint64 consumed = 0;
    bool truncated = false;
    while (!file.atEnd() && lineNo < kMaxLines && consumed < kMaxBytes) {
        const QByteArray line = file.readLine();
        consumed += line.size();
        ++lineNo;
        QJsonParseError error{};
        const QJsonDocument document = QJsonDocument::fromJson(line.trimmed(), &error);
        const bool parsed = error.error == QJsonParseError::NoError && !document.isNull();
        const QString rendered = parsed
            ? QString::fromUtf8(document.toJson(QJsonDocument::Indented)).trimmed()
            : QString::fromUtf8(line).trimmed();
        QVariantMap row;
        row.insert(QStringLiteral("line"), lineNo);
        row.insert(QStringLiteral("ok"), parsed);
        row.insert(QStringLiteral("text"), rendered);
        row.insert(QStringLiteral("error"), parsed ? QString{} : error.errorString());
        rows.push_back(row);
        stream << QStringLiteral("#%1 ").arg(lineNo);
        if (!parsed) stream << QStringLiteral("JSON error: ") << error.errorString() << QStringLiteral("\n");
        stream << rendered << QStringLiteral("\n\n");
    }
    truncated = !file.atEnd();

    QVariantMap out = previewOkBase(info, QStringLiteral("jsonl"));
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("lineCount"), lineNo);
    out.insert(QStringLiteral("truncated"), truncated);
    out.insert(QStringLiteral("summaryText"), QStringLiteral("%1, shown lines: %2%3")
        .arg(bytesText(static_cast<std::uint64_t>(info.size())))
        .arg(lineNo)
        .arg(truncated ? QStringLiteral(" (file is truncated for preview)") : QString{}));
    out.insert(QStringLiteral("contentText"), content.trimmed());
    return out;
}

QVariantMap CompressionViewModel::previewArtifact(const QString& path) const {
    const QString previewPath = path.trimmed().isEmpty() ? selectedArtifactFile() : path.trimmed();
    QFileInfo info(previewPath);
    if (previewPath.isEmpty()) return previewError(previewPath, QStringLiteral("binary artifact is not selected"));
    if (!info.exists()) return previewError(previewPath, QStringLiteral("binary artifact has not been created yet"));

    QFile file(previewPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return previewError(previewPath, QStringLiteral("failed to open binary artifact"));
    }

    constexpr qint64 kPreviewBytes = 4096;
    const QByteArray bytes = file.read(kPreviewBytes);
    QVariantMap out = previewOkBase(info, QStringLiteral("artifact"));
    out.insert(QStringLiteral("summaryText"), QStringLiteral("%1, hex preview: %2 bytes%3")
        .arg(bytesText(static_cast<std::uint64_t>(info.size())))
        .arg(bytes.size())
        .arg(info.size() > bytes.size() ? QStringLiteral(" (file start)") : QString{}));
    out.insert(QStringLiteral("contentText"), hexDumpText(bytes));
    out.insert(QStringLiteral("truncated"), info.size() > bytes.size());

    QVariantList metadata;
    metadata.push_back(metadataRow(QStringLiteral("File"), info.fileName()));
    metadata.push_back(metadataRow(QStringLiteral("Size"), bytesText(static_cast<std::uint64_t>(info.size()))));
    metadata.push_back(metadataRow(QStringLiteral("Method"), selectedPipelineLabel()));
    metadata.push_back(metadataRow(QStringLiteral("Pipeline ID"), selectedPipelineId_));

    QVariantList blocks;
    if (info.suffix().compare(QStringLiteral("hfc"), Qt::CaseInsensitive) == 0) {
        const auto hfc = hft_compressor::openHfcFile(previewPath.toStdString());
        metadata.push_back(metadataRow(QStringLiteral("HFC status"), viewString(hft_compressor::statusToString(hfc.status))));
        if (hft_compressor::isOk(hfc.status)) {
            metadata.push_back(metadataRow(QStringLiteral("Version"), QString::number(hfc.version)));
            metadata.push_back(metadataRow(QStringLiteral("Codec"), QString::number(hfc.codec)));
            metadata.push_back(metadataRow(QStringLiteral("Block bytes"), bytesText(hfc.blockBytes)));
            metadata.push_back(metadataRow(QStringLiteral("Input"), bytesText(hfc.inputBytes)));
            metadata.push_back(metadataRow(QStringLiteral("Output"), bytesText(hfc.outputBytes)));
            metadata.push_back(metadataRow(QStringLiteral("Lines"), QString::number(hfc.lineCount)));
            metadata.push_back(metadataRow(QStringLiteral("Blocks"), QString::number(hfc.blockCount)));
            const std::size_t limit = std::min<std::size_t>(hfc.blocks.size(), 24u);
            for (std::size_t i = 0; i < limit; ++i) {
                const auto& block = hfc.blocks[i];
                QVariantMap row;
                row.insert(QStringLiteral("index"), static_cast<int>(i + 1u));
                row.insert(QStringLiteral("text"), QStringLiteral("#%1  offset %2  %3 -> %4  lines %5")
                    .arg(static_cast<qulonglong>(i + 1u))
                    .arg(static_cast<qulonglong>(block.fileOffset))
                    .arg(bytesText(block.uncompressedBytes))
                    .arg(bytesText(block.compressedBytes))
                    .arg(static_cast<qulonglong>(block.lineCount)));
                blocks.push_back(row);
            }
        } else if (!hfc.error.empty()) {
            metadata.push_back(metadataRow(QStringLiteral("HFC error"), QString::fromStdString(hfc.error)));
        }
    } else {
        metadata.push_back(metadataRow(QStringLiteral("Format"), info.suffix().isEmpty() ? QStringLiteral("unknown") : info.suffix().toUpper()));
        metadata.push_back(metadataRow(QStringLiteral("Structure"), QStringLiteral("generic binary preview")));
    }

    out.insert(QStringLiteral("metadataRows"), metadata);
    out.insert(QStringLiteral("blockRows"), blocks);
    return out;
}

void CompressionViewModel::runCompression() {
    if (!canRun()) return;
    hft_compressor::CompressionRequest request{};
    request.inputPath = inputFile().toStdString();
    request.outputPathOverride = outputFilePreview().toStdString();
    request.pipelineId = selectedPipelineId_.toStdString();
    running_ = true;
    statusText_ = QStringLiteral("Кодирование выполняется...");
    emit runningChanged();
    emit resultChanged();
    emit canRunChanged();
    emit canDecodeVerifyChanged();

    QPointer<CompressionViewModel> self(this);
    std::thread([self, request]() {
        const auto result = hft_compressor::compress(request);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result]() {
            if (self) self->applyResult_(result);
        }, Qt::QueuedConnection);
    }).detach();
}

void CompressionViewModel::runAllAvailableChannels() {
    runCompression();
}

void CompressionViewModel::decodeVerifySelected() {
    if (!canDecodeVerify()) return;
    hft_compressor::DecodeVerifyRequest request{};
    request.compressedPath = encodedArtifactPath_(selectedChannel_, selectedPipelineId_).toStdString();
    request.canonicalPath = inputFile().toStdString();
    request.pipelineId = selectedPipelineId_.toStdString();

    verifying_ = true;
    verifyStatusText_ = QStringLiteral("Декодирование и проверка выполняются...");
    QVariantMap pendingRow;
    pendingRow.insert(QStringLiteral("pipelineId"), selectedPipelineId_);
    pendingRow.insert(QStringLiteral("pipelineLabel"), selectedPipelineLabel());
    pendingRow.insert(QStringLiteral("stream"), selectedChannel_);
    pendingRow.insert(QStringLiteral("streamLabel"), displayChannel(selectedChannel_));
    pendingRow.insert(QStringLiteral("status"), QStringLiteral("running"));
    pendingRow.insert(QStringLiteral("ok"), false);
    pendingRow.insert(QStringLiteral("verified"), false);
    pendingRow.insert(QStringLiteral("exactText"), QStringLiteral("running"));
    pendingRow.insert(QStringLiteral("decodeMbPerSec"), 0.0);
    pendingRow.insert(QStringLiteral("decodeText"), QStringLiteral("декодирую..."));
    pendingRow.insert(QStringLiteral("decodedSizeText"), QStringLiteral("0 bytes"));
    pendingRow.insert(QStringLiteral("canonicalSizeText"), bytesText(static_cast<std::uint64_t>(QFileInfo(inputFile()).size())));
    pendingRow.insert(QStringLiteral("sizeText"), QStringLiteral("декодирование выполняется"));
    pendingRow.insert(QStringLiteral("mismatchPercent"), 0.0);
    pendingRow.insert(QStringLiteral("mismatchPercentText"), QStringLiteral("0.00%"));
    pendingRow.insert(QStringLiteral("compressedFile"), QString::fromStdString(request.compressedPath.string()));
    pendingRow.insert(QStringLiteral("canonicalFile"), QString::fromStdString(request.canonicalPath.string()));
    pendingRow.insert(QStringLiteral("source"), QStringLiteral("running"));
    appendVerifyRow_(pendingRow);
    emit verifyingChanged();
    emit verifyResultChanged();
    emit verifyRowsChanged();
    emit channelStatsChanged();
    emit canDecodeVerifyChanged();

    QPointer<CompressionViewModel> self(this);
    std::thread([self, request]() {
        const auto result = hft_compressor::decodeAndVerify(request);
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result]() {
            if (self) self->applyVerifyResult_(result);
        }, Qt::QueuedConnection);
    }).detach();
}

void CompressionViewModel::decodeVerifyAllAvailable() {
    decodeVerifySelected();
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

QString CompressionViewModel::outputFilePreviewFor_(const QString& channel) const {
    const QString root = outputRoot();
    QString baseName = channelFileName_(channel);
    if (root.isEmpty() || baseName.isEmpty()) return {};
    const auto* pipeline = findPipeline(selectedPipelineId_);
    const QString extension = pipeline == nullptr || pipeline->fileExtension.empty()
        ? QStringLiteral(".hfc")
        : viewString(pipeline->fileExtension);
    const QString safePipelineId = selectedPipelineId_.trimmed().isEmpty()
        ? QStringLiteral("unknown")
        : QString(selectedPipelineId_).replace('.', '_');
    baseName.replace(QStringLiteral(".jsonl"), QStringLiteral(".%1%2").arg(safePipelineId, extension));
    return QDir(root).absoluteFilePath(baseName);
}

QString CompressionViewModel::encodedArtifactPath_(const QString& channel, const QString& pipelineId) const {
    const QString stream = channel.trimmed().toLower();
    const QString pipeline = pipelineId.trimmed();
    for (const auto& value : runRows_) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("stream")).toString() != stream) continue;
        if (row.value(QStringLiteral("pipelineId")).toString() != pipeline) continue;
        if (!row.value(QStringLiteral("ok")).toBool()) continue;
        const QString path = row.value(QStringLiteral("outputFile")).toString();
        if (!path.isEmpty() && QFileInfo::exists(path)) return path;
    }
    return {};
}

bool CompressionViewModel::encodedArtifactExists_(const QString& channel, const QString& pipelineId) const {
    return !encodedArtifactPath_(channel, pipelineId).isEmpty();
}

QString CompressionViewModel::verifyFilePreviewFor_(const QString& channel) const {
    const QString encodedPath = encodedArtifactPath_(channel, selectedPipelineId_);
    if (!encodedPath.isEmpty()) return encodedPath;
    const QString root = outputRoot();
    QString baseName = channelFileName_(channel);
    if (root.isEmpty() || baseName.isEmpty()) return {};
    const auto* pipeline = findPipeline(selectedPipelineId_);
    const QString extension = pipeline == nullptr || pipeline->fileExtension.empty()
        ? QStringLiteral(".hfc")
        : viewString(pipeline->fileExtension);
    const QString safePipelineId = selectedPipelineId_.trimmed().isEmpty()
        ? QStringLiteral("unknown")
        : QString(selectedPipelineId_).replace('.', '_');
    baseName.replace(QStringLiteral(".jsonl"), QStringLiteral(".%1%2").arg(safePipelineId, extension));
    return QDir(root).absoluteFilePath(baseName);
}

void CompressionViewModel::reloadStoredRunRows_() {
    runRows_.clear();
    const QString root = outputRoot();
    if (!root.isEmpty()) {
        QDir dir(root);
        const auto files = dir.entryList(QStringList{QStringLiteral("*.metrics.json")}, QDir::Files, QDir::Name);
        for (const auto& file : files) {
            const QVariantMap row = metricsRow_(dir.absoluteFilePath(file));
            if (!row.isEmpty()) runRows_.push_back(row);
        }
    }
    emit runRowsChanged();
    emit artifactAvailabilityChanged();
}

void CompressionViewModel::reloadStoredVerifyRows_() {
    verifyRows_.clear();
    const QString root = outputRoot();
    if (!root.isEmpty()) {
        QDir dir(root);
        const auto files = dir.entryList(QStringList{QStringLiteral("*.verify.json")}, QDir::Files, QDir::Name);
        for (const auto& file : files) {
            const QVariantMap row = verifyMetricsRow_(dir.absoluteFilePath(file));
            if (!row.isEmpty()) verifyRows_.push_back(row);
        }
    }
    emit verifyRowsChanged();
}

void CompressionViewModel::appendResultRow_(const QVariantMap& row) {
    const QString key = row.value(QStringLiteral("pipelineId")).toString() + QStringLiteral("|") + row.value(QStringLiteral("stream")).toString();
    for (int i = 0; i < runRows_.size(); ++i) {
        const QVariantMap existing = runRows_[i].toMap();
        const QString existingKey = existing.value(QStringLiteral("pipelineId")).toString() + QStringLiteral("|") + existing.value(QStringLiteral("stream")).toString();
        if (existingKey == key) {
            runRows_[i] = row;
            return;
        }
    }
    runRows_.push_back(row);
}

void CompressionViewModel::appendVerifyRow_(const QVariantMap& row) {
    const QString key = row.value(QStringLiteral("pipelineId")).toString() + QStringLiteral("|") + row.value(QStringLiteral("stream")).toString();
    for (int i = 0; i < verifyRows_.size(); ++i) {
        const QVariantMap existing = verifyRows_[i].toMap();
        const QString existingKey = existing.value(QStringLiteral("pipelineId")).toString() + QStringLiteral("|") + existing.value(QStringLiteral("stream")).toString();
        if (existingKey == key) {
            verifyRows_.removeAt(i);
            verifyRows_.push_back(row);
            return;
        }
    }
    verifyRows_.push_back(row);
}

void CompressionViewModel::applyResult_(const hft_compressor::CompressionResult& result) {
    applyResults_(std::vector<hft_compressor::CompressionResult>{result});
}

void CompressionViewModel::applyResults_(const std::vector<hft_compressor::CompressionResult>& results) {
    for (const auto& result : results) {
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
        appendResultRow_(resultRow_(result));
        statusText_ = hft_compressor::isOk(result.status)
            ? QStringLiteral("Кодирование завершено")
            : QStringLiteral("Ошибка кодирования: ") + QString::fromStdString(result.error);
    }
    if (results.size() > 1u) statusText_ = QStringLiteral("Пакетное кодирование завершено");
    running_ = false;
    emit runningChanged();
    emit resultChanged();
    emit runRowsChanged();
    emit channelStatsChanged();
    emit canRunChanged();
    emit canDecodeVerifyChanged();
    emit artifactAvailabilityChanged();
}

void CompressionViewModel::applyVerifyResult_(const hft_compressor::DecodeVerifyResult& result) {
    applyVerifyResults_(std::vector<hft_compressor::DecodeVerifyResult>{result});
}

void CompressionViewModel::applyVerifyResults_(const std::vector<hft_compressor::DecodeVerifyResult>& results) {
    for (const auto& result : results) {
        verifyFile_ = QString::fromStdString(result.compressedPath.string());
        verifyCanonicalFile_ = QString::fromStdString(result.canonicalPath.string());
        verifyDecodedBytes_ = result.decodedBytes;
        verifyCanonicalBytes_ = result.canonicalBytes;
        verifyMismatchOffset_ = result.firstMismatchOffset;
        verifyDecodeMbPerSec_ = hft_compressor::decodeMbPerSec(result);
        verifyExact_ = result.verified;
        appendVerifyRow_(verifyRow_(result));
        verifyStatusText_ = hft_compressor::isOk(result.status)
            ? QStringLiteral("Декодирование и проверка завершены")
            : QStringLiteral("Ошибка проверки: ") + QString::fromStdString(result.error);
    }
    if (results.size() > 1u) verifyStatusText_ = QStringLiteral("Пакетная проверка завершена");
    verifying_ = false;
    emit verifyingChanged();
    emit verifyResultChanged();
    emit verifyRowsChanged();
    emit channelStatsChanged();
    emit canDecodeVerifyChanged();
}

QVariantMap CompressionViewModel::resultRow_(const hft_compressor::CompressionResult& result) const {
    QVariantMap row;
    const double currentRatio = hft_compressor::ratio(result);
    const double encode = hft_compressor::encodeMbPerSec(result);
    const double decode = hft_compressor::decodeMbPerSec(result);
    row.insert(QStringLiteral("pipelineId"), QString::fromStdString(result.pipelineId));
    row.insert(QStringLiteral("pipelineLabel"), selectedPipelineLabel());
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
    const QString pipelineId = jsonString(object, "pipeline_id");
    if (stream.isEmpty() || pipelineId.isEmpty()) return {};
    const auto inputBytes = jsonUInt64(object, "input_bytes");
    const auto outputBytes = jsonUInt64(object, "output_bytes");
    const double currentRatio = object.value(QStringLiteral("compression_ratio")).toDouble();
    const double encode = object.value(QStringLiteral("encode_mb_per_sec")).toDouble();
    const double decode = object.value(QStringLiteral("decode_mb_per_sec")).toDouble();
    QVariantMap row;
    row.insert(QStringLiteral("pipelineId"), pipelineId);
    row.insert(QStringLiteral("pipelineLabel"), pipelineLabelFor(pipelineId));
    row.insert(QStringLiteral("stream"), stream);
    row.insert(QStringLiteral("streamLabel"), displayChannel(stream));
    row.insert(QStringLiteral("profile"), jsonString(object, "profile"));
    row.insert(QStringLiteral("profileLabel"), displayProfile(jsonString(object, "profile")));
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
    row.insert(QStringLiteral("outputFile"), artifactPathFromMetricsPath(metricsPath, pipelineId, QStringLiteral(".metrics")));
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


