#include "gui/viewmodels/CompressionViewModel.hpp"

#include "gui/viewmodels/CompressionViewModelHelpers.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTextStream>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "hft_compressor/compressor.hpp"

namespace hftrec::gui {
using namespace compression_vm;

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

QVariantMap CompressionViewModel::previewEncodedJson(const QString& path) const {
    const QString previewPath = path.trimmed().isEmpty() ? selectedArtifactFile() : path.trimmed();
    QFileInfo info(previewPath);
    if (previewPath.isEmpty()) return previewError(previewPath, QStringLiteral("encoded artifact is not selected"));
    if (!info.exists()) return previewError(previewPath, QStringLiteral("encoded artifact has not been created yet"));

    std::string content;
    constexpr std::size_t kMaxPreviewBytes = 512u * 1024u;
    std::uint64_t producedBytes = 0;
    const auto status = hft_compressor::inspectCompressedArtifact(
        previewPath.toStdString(),
        selectedPipelineId_.toStdString(),
        std::string_view{"encoded-json"},
        [&](std::span<const std::uint8_t> block) noexcept -> bool {
            producedBytes += static_cast<std::uint64_t>(block.size());
            const std::size_t remaining = content.size() >= kMaxPreviewBytes ? 0u : kMaxPreviewBytes - content.size();
            const std::size_t take = std::min<std::size_t>(remaining, block.size());
            if (take != 0u) content.append(reinterpret_cast<const char*>(block.data()), take);
            return content.size() < kMaxPreviewBytes;
        });
    if (!hft_compressor::isOk(status) && status != hft_compressor::Status::CallbackStopped) {
        return previewError(previewPath, QStringLiteral("encoded-json inspect is not available for this artifact"));
    }

    QVariantMap out = previewOkBase(info, QStringLiteral("encoded-json"));
    const bool truncated = producedBytes > content.size() || status == hft_compressor::Status::CallbackStopped;
    out.insert(QStringLiteral("summaryText"), QStringLiteral("%1, encoded JSON preview: %2%3")
        .arg(bytesText(static_cast<std::uint64_t>(info.size())))
        .arg(bytesText(static_cast<std::uint64_t>(content.size())))
        .arg(truncated ? QStringLiteral(" (truncated)") : QString{}));
    out.insert(QStringLiteral("contentText"), QString::fromUtf8(content.data(), static_cast<qsizetype>(content.size())).trimmed());
    out.insert(QStringLiteral("truncated"), truncated);

    QVariantList metadata;
    metadata.push_back(metadataRow(QStringLiteral("View"), QStringLiteral("encoded-json")));
    metadata.push_back(metadataRow(QStringLiteral("Method"), selectedPipelineLabel()));
    metadata.push_back(metadataRow(QStringLiteral("Pipeline ID"), selectedPipelineId_));
    metadata.push_back(metadataRow(QStringLiteral("Source"), info.fileName()));
    out.insert(QStringLiteral("metadataRows"), metadata);
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

}  // namespace hftrec::gui
