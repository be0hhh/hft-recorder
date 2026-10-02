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

QVariantList CompressionViewModel::verifyRows() const {
    return rowsForSelectedChannel_(verifyRows_);
}

QVariantList CompressionViewModel::decodeSpeedSeries() const {
    return verifyRows();
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

bool CompressionViewModel::canDecodeVerify() const {
    return !verifying_
        && !running_
        && selectedPipelineAvailable()
        && QFileInfo::exists(inputFile())
        && encodedArtifactExists_(selectedChannel_, selectedPipelineId_);
}

void CompressionViewModel::decodeVerifySelected() {
    if (!canDecodeVerify()) return;
    if (isPythonPipelineId(selectedPipelineId_)) {
        const QString artifact = encodedArtifactPath_(selectedChannel_, selectedPipelineId_);
        const QString canonical = inputFile();
        const QString pipelineId = selectedPipelineId_;
        verifying_ = true;
        verifyStatusText_ = QStringLiteral("Python декодирование и проверка выполняются...");
        QVariantMap pendingRow;
        pendingRow.insert(QStringLiteral("pipelineId"), pipelineId);
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
        pendingRow.insert(QStringLiteral("canonicalSizeText"), bytesText(static_cast<std::uint64_t>(QFileInfo(canonical).size())));
        pendingRow.insert(QStringLiteral("compressedSizeText"), bytesText(static_cast<std::uint64_t>(QFileInfo(artifact).size())));
        pendingRow.insert(QStringLiteral("sizeText"), QStringLiteral("декодирование выполняется"));
        pendingRow.insert(QStringLiteral("mismatchPercent"), 0.0);
        pendingRow.insert(QStringLiteral("mismatchPercentText"), QStringLiteral("0.00%"));
        pendingRow.insert(QStringLiteral("compressedFile"), artifact);
        pendingRow.insert(QStringLiteral("canonicalFile"), canonical);
        pendingRow.insert(QStringLiteral("source"), QStringLiteral("running"));
        appendVerifyRow_(pendingRow);
        emit verifyingChanged();
        emit verifyResultChanged();
        emit verifyRowsChanged();
        emit channelStatsChanged();
        emit canDecodeVerifyChanged();

        QPointer<CompressionViewModel> self(this);
        std::thread([self, artifact, canonical, pipelineId]() {
            const QJsonDocument document = runPythonCodecCli(QStringList{
                QStringLiteral("verify"),
                artifact,
                canonical,
                QStringLiteral("--codec"),
                pipelineId,
            }, 600000);
            QVariantMap result = document.isObject() ? document.object().toVariantMap() : QVariantMap{};
            if (result.isEmpty()) {
                result.insert(QStringLiteral("status"), QStringLiteral("decode_error"));
                result.insert(QStringLiteral("ok"), false);
                result.insert(QStringLiteral("codec_id"), pipelineId);
                result.insert(QStringLiteral("artifact_path"), artifact);
                result.insert(QStringLiteral("canonical_path"), canonical);
                result.insert(QStringLiteral("error"), QStringLiteral("Python codec process failed"));
            }
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, result]() {
                if (self) self->applyPythonVerifyResult_(result);
            }, Qt::QueuedConnection);
        }).detach();
        return;
    }
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
    pendingRow.insert(QStringLiteral("compressedSizeText"), bytesText(static_cast<std::uint64_t>(QFileInfo(QString::fromStdString(request.compressedPath.string())).size())));
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
    if (running_ || verifying_) return;
    const QString canonical = inputFile();
    if (canonical.isEmpty() || !QFileInfo::exists(canonical)) return;

    QVariantList rows;
    for (const auto& value : pipelines()) {
        QVariantMap row = value.toMap();
        const QString pipelineId = row.value(QStringLiteral("id")).toString();
        if (pipelineId.isEmpty()) continue;
        if (!variantBool(row, QStringLiteral("available"))) continue;
        const QString artifact = encodedArtifactPath_(selectedChannel_, pipelineId);
        if (artifact.isEmpty()) continue;
        row.insert(QStringLiteral("artifact"), artifact);
        rows.push_back(row);
    }
    if (rows.empty()) return;

    verifying_ = true;
    verifyStatusText_ = QStringLiteral("Verification batch running...");
    emit verifyingChanged();
    emit verifyResultChanged();
    emit canRunChanged();
    emit canDecodeVerifyChanged();

    QPointer<CompressionViewModel> self(this);
    std::thread([self, canonical, rows]() {
        std::vector<hft_compressor::DecodeVerifyResult> cppResults;
        QVariantList pythonResults;
        for (const auto& value : rows) {
            const QVariantMap row = value.toMap();
            const QString pipelineId = row.value(QStringLiteral("id")).toString();
            const QString artifact = row.value(QStringLiteral("artifact")).toString();
            if (pipelineId.isEmpty() || artifact.isEmpty()) continue;
            if (isPythonPipelineId(pipelineId)) {
                const QJsonDocument document = runPythonCodecCli(QStringList{QStringLiteral("verify"), artifact, canonical, QStringLiteral("--codec"), pipelineId}, 600000);
                QVariantMap result = document.isObject() ? document.object().toVariantMap() : QVariantMap{};
                if (result.isEmpty()) {
                    result.insert(QStringLiteral("status"), QStringLiteral("decode_error"));
                    result.insert(QStringLiteral("ok"), false);
                    result.insert(QStringLiteral("codec_id"), pipelineId);
                    result.insert(QStringLiteral("artifact_path"), artifact);
                    result.insert(QStringLiteral("canonical_path"), canonical);
                }
                pythonResults.push_back(result);
                continue;
            }
            hft_compressor::DecodeVerifyRequest request{};
            request.compressedPath = artifact.toStdString();
            request.canonicalPath = canonical.toStdString();
            request.pipelineId = pipelineId.toStdString();
            cppResults.push_back(hft_compressor::decodeAndVerify(request));
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, cppResults, pythonResults]() {
            if (!self) return;
            if (!cppResults.empty()) self->applyVerifyResults_(cppResults);
            for (const auto& value : pythonResults) self->applyPythonVerifyResult_(value.toMap());
            self->verifying_ = false;
            self->verifyStatusText_ = QStringLiteral("Verification batch complete");
            emit self->verifyingChanged();
            emit self->verifyResultChanged();
            emit self->verifyRowsChanged();
            emit self->channelStatsChanged();
            emit self->canRunChanged();
            emit self->canDecodeVerifyChanged();
        }, Qt::QueuedConnection);
    }).detach();
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

void CompressionViewModel::applyPythonVerifyResult_(const QVariantMap& result) {
    const QString pipelineId = variantString(result, QStringLiteral("codec_id"), selectedPipelineId_);
    const QString canonical = variantString(result, QStringLiteral("canonical_path"), inputFile());
    const QString stream = viewString(hft_compressor::streamTypeToString(hft_compressor::inferStreamTypeFromPath(canonical.toStdString())));
    const auto decodedBytes = static_cast<std::uint64_t>(result.value(QStringLiteral("decoded_bytes")).toULongLong());
    const auto canonicalBytes = static_cast<std::uint64_t>(result.value(QStringLiteral("canonical_bytes")).toULongLong());
    const auto decodeNs = static_cast<std::uint64_t>(result.value(QStringLiteral("decode_ns")).toULongLong());
    const auto mismatchBytes = static_cast<std::uint64_t>(result.value(QStringLiteral("mismatch_bytes")).toULongLong());
    const double mismatchPercent = canonicalBytes == 0u ? 0.0 : (static_cast<double>(mismatchBytes) / static_cast<double>(canonicalBytes)) * 100.0;
    const double decode = mbpsValue(decodedBytes, decodeNs);
    const bool verified = variantBool(result, QStringLiteral("ok"));

    QVariantMap row;
    row.insert(QStringLiteral("pipelineId"), pipelineId);
    row.insert(QStringLiteral("pipelineLabel"), pipelineLabelFor(pipelineId));
    row.insert(QStringLiteral("stream"), stream);
    row.insert(QStringLiteral("streamLabel"), displayChannel(stream));
    row.insert(QStringLiteral("profile"), QStringLiteral("research"));
    row.insert(QStringLiteral("profileLabel"), displayProfile(QStringLiteral("research")));
    row.insert(QStringLiteral("status"), variantString(result, QStringLiteral("status"), QStringLiteral("decode_error")));
    row.insert(QStringLiteral("ok"), verified);
    row.insert(QStringLiteral("verified"), verified);
    row.insert(QStringLiteral("exactText"), verified ? QStringLiteral("exact") : QStringLiteral("mismatch"));
    const auto compressedBytes = static_cast<std::uint64_t>(QFileInfo(result.value(QStringLiteral("artifact_path")).toString()).size());
    row.insert(QStringLiteral("compressedBytes"), static_cast<qulonglong>(compressedBytes));
    row.insert(QStringLiteral("decodedBytes"), static_cast<qulonglong>(decodedBytes));
    row.insert(QStringLiteral("canonicalBytes"), static_cast<qulonglong>(canonicalBytes));
    row.insert(QStringLiteral("comparedBytes"), static_cast<qulonglong>(std::min(decodedBytes, canonicalBytes)));
    row.insert(QStringLiteral("mismatchBytes"), static_cast<qulonglong>(mismatchBytes));
    row.insert(QStringLiteral("mismatchPercent"), mismatchPercent);
    row.insert(QStringLiteral("mismatchPercentText"), percentLabel(mismatchPercent));
    row.insert(QStringLiteral("lineCount"), static_cast<qulonglong>(result.value(QStringLiteral("decoded_record_count")).toULongLong()));
    row.insert(QStringLiteral("blockCount"), static_cast<qulonglong>(1));
    row.insert(QStringLiteral("decodeMbPerSec"), decode);
    row.insert(QStringLiteral("decodeText"), mbps(decode));
    row.insert(QStringLiteral("compressedSizeText"), bytesText(compressedBytes));
    row.insert(QStringLiteral("decodedSizeText"), bytesText(decodedBytes));
    row.insert(QStringLiteral("canonicalSizeText"), bytesText(canonicalBytes));
    row.insert(QStringLiteral("sizeText"), bytesText(decodedBytes) + QStringLiteral(" / эталон ") + bytesText(canonicalBytes));
    row.insert(QStringLiteral("mismatchOffset"), static_cast<qulonglong>(0));
    row.insert(QStringLiteral("mismatchText"), verified ? QStringLiteral("совпадает") : QStringLiteral("не совпало на %1").arg(percentLabel(mismatchPercent)));
    row.insert(QStringLiteral("compressedFile"), result.value(QStringLiteral("artifact_path")).toString());
    row.insert(QStringLiteral("canonicalFile"), canonical);
    row.insert(QStringLiteral("metricsFile"), QString{});
    row.insert(QStringLiteral("source"), QStringLiteral("python"));

    verifyFile_ = row.value(QStringLiteral("compressedFile")).toString();
    verifyCanonicalFile_ = canonical;
    verifyDecodedBytes_ = decodedBytes;
    verifyCanonicalBytes_ = canonicalBytes;
    verifyMismatchOffset_ = 0;
    verifyDecodeMbPerSec_ = decode;
    verifyExact_ = verified;
    appendVerifyRow_(row);
    verifyStatusText_ = verified
        ? QStringLiteral("Python декодирование совпадает с JSONL")
        : QStringLiteral("Ошибка Python проверки: ") + variantString(result, QStringLiteral("error"), row.value(QStringLiteral("status")).toString());
    verifying_ = false;
    emit verifyingChanged();
    emit verifyResultChanged();
    emit verifyRowsChanged();
    emit channelStatsChanged();
    emit canDecodeVerifyChanged();
}


}  // namespace hftrec::gui
