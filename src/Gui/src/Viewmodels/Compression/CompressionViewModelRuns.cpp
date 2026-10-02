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

QVariantList CompressionViewModel::runRows() const {
    return rowsForSelectedChannel_(runRows_);
}

QVariantList CompressionViewModel::speedSeries() const {
    return runRows();
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

void CompressionViewModel::runCompression() {
    if (!canRun()) return;
    if (isPythonPipelineId(selectedPipelineId_)) {
        const QString input = inputFile();
        const QString output = outputRoot();
        const QString pipelineId = selectedPipelineId_;
        running_ = true;
        statusText_ = QStringLiteral("Кодирование Python выполняется...");
        emit runningChanged();
        emit resultChanged();
        emit canRunChanged();
        emit canDecodeVerifyChanged();

        QPointer<CompressionViewModel> self(this);
        std::thread([self, input, output, pipelineId]() {
            const QJsonDocument document = runPythonCodecCli(QStringList{
                QStringLiteral("compress"),
                input,
                QStringLiteral("--codec"),
                pipelineId,
                QStringLiteral("--output-root"),
                output,
            }, 600000);
            QVariantMap result = document.isObject() ? document.object().toVariantMap() : QVariantMap{};
            if (result.isEmpty()) {
                result.insert(QStringLiteral("status"), QStringLiteral("decode_error"));
                result.insert(QStringLiteral("ok"), false);
                result.insert(QStringLiteral("codec_id"), pipelineId);
                result.insert(QStringLiteral("input_path"), input);
                result.insert(QStringLiteral("error"), QStringLiteral("Python codec process failed"));
            }
            if (!self) return;
            QMetaObject::invokeMethod(self, [self, result]() {
                if (self) self->applyPythonResult_(result);
            }, Qt::QueuedConnection);
        }).detach();
        return;
    }
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
    runAllAvailablePipelines();
}

void CompressionViewModel::runAllAvailablePipelines() {
    if (running_ || verifying_) return;
    const QString input = inputFile();
    if (input.isEmpty() || !QFileInfo::exists(input)) return;

    QVariantList rows;
    for (const auto& value : pipelines()) {
        QVariantMap row = value.toMap();
        if (!variantBool(row, QStringLiteral("available"))) continue;
        const QString pipelineId = row.value(QStringLiteral("id")).toString();
        row.insert(QStringLiteral("outputFilePreview"), outputFilePreviewFor_(selectedChannel_, pipelineId));
        rows.push_back(row);
    }
    if (rows.empty()) return;

    const QString output = outputRoot();
    running_ = true;
    statusText_ = QStringLiteral("Compression batch running...");
    emit runningChanged();
    emit resultChanged();
    emit canRunChanged();
    emit canDecodeVerifyChanged();

    QPointer<CompressionViewModel> self(this);
    std::thread([self, input, output, rows]() {
        std::vector<hft_compressor::CompressionResult> cppResults;
        QVariantList pythonResults;
        for (const auto& value : rows) {
            const QVariantMap row = value.toMap();
            const QString pipelineId = row.value(QStringLiteral("id")).toString();
            const QString outputFilePreview = row.value(QStringLiteral("outputFilePreview")).toString();
            if (pipelineId.isEmpty()) continue;
            if (isPythonPipelineId(pipelineId)) {
                const QJsonDocument document = runPythonCodecCli(QStringList{QStringLiteral("compress"), input, QStringLiteral("--codec"), pipelineId, QStringLiteral("--output-root"), output}, 600000);
                QVariantMap result = document.isObject() ? document.object().toVariantMap() : QVariantMap{};
                if (result.isEmpty()) {
                    result.insert(QStringLiteral("status"), QStringLiteral("decode_error"));
                    result.insert(QStringLiteral("ok"), false);
                    result.insert(QStringLiteral("codec_id"), pipelineId);
                    result.insert(QStringLiteral("input_path"), input);
                    result.insert(QStringLiteral("error"), QStringLiteral("Python codec process failed"));
                }
                pythonResults.push_back(result);
                continue;
            }
            try {
                hft_compressor::CompressionRequest request{};
                request.inputPath = input.toStdString();
                request.outputPathOverride = outputFilePreview.toStdString();
                request.pipelineId = pipelineId.toStdString();
                cppResults.push_back(hft_compressor::compress(request));
            } catch (const std::exception& exception) {
                cppResults.push_back(failedCppCompressionResult(row, input, outputFilePreview, QString::fromUtf8(exception.what())));
            } catch (...) {
                cppResults.push_back(failedCppCompressionResult(row, input, outputFilePreview, QStringLiteral("unknown C++ codec failure")));
            }
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, cppResults, pythonResults]() {
            if (!self) return;
            if (!cppResults.empty()) self->applyResults_(cppResults);
            for (const auto& value : pythonResults) self->applyPythonResult_(value.toMap());
            self->running_ = false;
            self->statusText_ = QStringLiteral("Compression batch complete");
            emit self->runningChanged();
            emit self->resultChanged();
            emit self->runRowsChanged();
            emit self->channelStatsChanged();
            emit self->canRunChanged();
            emit self->canDecodeVerifyChanged();
            emit self->artifactAvailabilityChanged();
        }, Qt::QueuedConnection);
    }).detach();
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

void CompressionViewModel::applyPythonResult_(const QVariantMap& result) {
    const QString pipelineId = variantString(result, QStringLiteral("codec_id"), selectedPipelineId_);
    const QString stream = variantString(result, QStringLiteral("stream"), selectedChannel_);
    const auto inputBytes = static_cast<std::uint64_t>(result.value(QStringLiteral("input_bytes")).toULongLong());
    const auto outputBytes = static_cast<std::uint64_t>(result.value(QStringLiteral("output_bytes")).toULongLong());
    const auto encodeNs = static_cast<std::uint64_t>(result.value(QStringLiteral("encode_ns")).toULongLong());
    const auto decodeNs = static_cast<std::uint64_t>(result.value(QStringLiteral("decode_ns")).toULongLong());
    const double ratio = variantDouble(result, QStringLiteral("ratio"), outputBytes == 0u ? 0.0 : static_cast<double>(inputBytes) / static_cast<double>(outputBytes));
    const double encode = mbpsValue(inputBytes, encodeNs);
    const double decode = mbpsValue(inputBytes, decodeNs);

    QVariantMap row;
    row.insert(QStringLiteral("pipelineId"), pipelineId);
    row.insert(QStringLiteral("pipelineLabel"), pipelineLabelFor(pipelineId));
    row.insert(QStringLiteral("stream"), stream);
    row.insert(QStringLiteral("streamLabel"), displayChannel(stream));
    row.insert(QStringLiteral("profile"), QStringLiteral("research"));
    row.insert(QStringLiteral("profileLabel"), displayProfile(QStringLiteral("research")));
    row.insert(QStringLiteral("status"), variantString(result, QStringLiteral("status"), QStringLiteral("decode_error")));
    row.insert(QStringLiteral("ok"), variantBool(result, QStringLiteral("ok")));
    row.insert(QStringLiteral("roundtrip"), variantBool(result, QStringLiteral("ok")));
    row.insert(QStringLiteral("inputBytes"), static_cast<qulonglong>(inputBytes));
    row.insert(QStringLiteral("outputBytes"), static_cast<qulonglong>(outputBytes));
    row.insert(QStringLiteral("lineCount"), static_cast<qulonglong>(result.value(QStringLiteral("decoded_record_count")).toULongLong()));
    row.insert(QStringLiteral("blockCount"), static_cast<qulonglong>(1));
    row.insert(QStringLiteral("ratio"), ratio);
    row.insert(QStringLiteral("ratioText"), ratioLabel(ratio));
    row.insert(QStringLiteral("spaceSavedText"), inputBytes == 0u ? QStringLiteral("0%") : QLocale().toString((1.0 - (static_cast<double>(outputBytes) / static_cast<double>(inputBytes))) * 100.0, 'f', 1) + QStringLiteral("%"));
    row.insert(QStringLiteral("encodeMbPerSec"), encode);
    row.insert(QStringLiteral("decodeMbPerSec"), decode);
    row.insert(QStringLiteral("encodeText"), mbps(encode));
    row.insert(QStringLiteral("decodeText"), mbps(decode));
    row.insert(QStringLiteral("sizeText"), bytesText(inputBytes) + QStringLiteral(" -> ") + bytesText(outputBytes));
    row.insert(QStringLiteral("outputFile"), result.value(QStringLiteral("output_path")).toString());
    row.insert(QStringLiteral("metricsFile"), result.value(QStringLiteral("metrics_path")).toString());
    row.insert(QStringLiteral("source"), QStringLiteral("python"));

    outputFile_ = row.value(QStringLiteral("outputFile")).toString();
    metricsFile_ = row.value(QStringLiteral("metricsFile")).toString();
    resultPipelineText_ = QStringLiteral("%1 | jsonl_bytes | raw_jsonl | python").arg(pipelineId);
    inputBytes_ = inputBytes;
    outputBytes_ = outputBytes;
    encodeNs_ = encodeNs;
    decodeNs_ = decodeNs;
    encodeCycles_ = 0;
    decodeCycles_ = 0;
    ratio_ = ratio;
    encodeMbPerSec_ = encode;
    decodeMbPerSec_ = decode;
    appendResultRow_(row);
    statusText_ = row.value(QStringLiteral("ok")).toBool()
        ? QStringLiteral("Python кодирование завершено")
        : QStringLiteral("Ошибка Python кодирования: ") + variantString(result, QStringLiteral("error"), row.value(QStringLiteral("status")).toString());
    running_ = false;
    emit runningChanged();
    emit resultChanged();
    emit runRowsChanged();
    emit channelStatsChanged();
    emit canRunChanged();
    emit canDecodeVerifyChanged();
    emit artifactAvailabilityChanged();
}


}  // namespace hftrec::gui
