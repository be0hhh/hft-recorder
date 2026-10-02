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

QString CompressionViewModel::outputFile() const { return outputFile_; }

QString CompressionViewModel::metricsFile() const { return metricsFile_; }

bool CompressionViewModel::hasEncodedArtifact(const QString& pipelineId) const {
    return encodedArtifactExists_(selectedChannel_, pipelineId.trimmed());
}

QString CompressionViewModel::firstEncodedPipelineId() const {
    for (const auto& pipeline : hft_compressor::listPipelines()) {
        if (pipeline.availability != hft_compressor::PipelineAvailability::Available) continue;
        if (!pipelineMatchesChannel(pipeline, selectedChannel_)) continue;
        const QString id = viewString(pipeline.id);
        if (encodedArtifactExists_(selectedChannel_, id)) return id;
    }
    for (const auto& value : pythonCodecPipelineRows()) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString();
        if (encodedArtifactExists_(selectedChannel_, id)) return id;
    }
    return {};
}

QString CompressionViewModel::outputFilePreviewFor_(const QString& channel) const {
    return outputFilePreviewFor_(channel, selectedPipelineId_);
}

QString CompressionViewModel::outputFilePreviewFor_(const QString& channel, const QString& pipelineId) const {
    const QString root = outputRoot();
    QString baseName = channelFileName_(channel);
    if (root.isEmpty() || baseName.isEmpty()) return {};
    if (isPythonPipelineId(pipelineId)) {
        baseName.replace(QStringLiteral(".jsonl"), QStringLiteral(".pylab%1").arg(pipelineFileExtensionFor(pipelineId)));
        const QString pythonSessionId = QFileInfo(inputFile()).dir().dirName().isEmpty()
            ? QStringLiteral("manual")
            : QFileInfo(inputFile()).dir().dirName();
        return QDir(root).absoluteFilePath(QStringLiteral("%1/sessions/%2/%3")
            .arg(pythonOutputSlugFor(pipelineId), pythonSessionId, baseName));
    }
    const QString extension = pipelineFileExtensionFor(pipelineId);
    baseName.replace(QStringLiteral(".jsonl"), extension);
    return QDir(root).absoluteFilePath(QStringLiteral("%1/sessions/%2/%3")
        .arg(pipelineOutputSlugFor(pipelineId), sessionIdForInputPath(inputFile()), baseName));
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
    return outputFilePreviewFor_(channel);
}

void CompressionViewModel::reloadStoredRunRows_() {
    runRows_.clear();
    const QString root = outputRoot();
    if (!root.isEmpty()) {
        QStringList paths;
        QDirIterator it(root, QStringList{QStringLiteral("*.metrics.json")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) paths.push_back(it.next());
        std::sort(paths.begin(), paths.end(), [](const QString& lhs, const QString& rhs) {
            const QFileInfo left(lhs);
            const QFileInfo right(rhs);
            if (left.lastModified() == right.lastModified()) return lhs < rhs;
            return left.lastModified() < right.lastModified();
        });
        for (const QString& path : paths) {
            const QVariantMap row = metricsRow_(path);
            if (!row.isEmpty()) appendResultRow_(row);
        }
    }
    emit runRowsChanged();
    emit artifactAvailabilityChanged();
}

void CompressionViewModel::reloadStoredVerifyRows_() {
    verifyRows_.clear();
    const QString root = outputRoot();
    if (!root.isEmpty()) {
        QStringList paths;
        QDirIterator it(root, QStringList{QStringLiteral("*.verify.json")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) paths.push_back(it.next());
        std::sort(paths.begin(), paths.end(), [](const QString& lhs, const QString& rhs) {
            const QFileInfo left(lhs);
            const QFileInfo right(rhs);
            if (left.lastModified() == right.lastModified()) return lhs < rhs;
            return left.lastModified() < right.lastModified();
        });
        for (const QString& path : paths) {
            const QVariantMap row = verifyMetricsRow_(path);
            if (!row.isEmpty()) appendVerifyRow_(row);
        }
    }
    emit verifyRowsChanged();
}


}  // namespace hftrec::gui
