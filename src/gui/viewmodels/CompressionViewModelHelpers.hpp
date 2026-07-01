#pragma once

#include <QByteArray>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <string_view>

#include "gui/backtests/BacktestSessionSummary.hpp"
#include "hft_compressor/compressor.hpp"
#include "hft_compressor/result.hpp"

namespace hftrec::gui::compression_vm {

QString mbps(double value);
QString bytesText(std::uint64_t value);
QString ratioLabel(double value);
QString percentLabel(double value);
QString viewString(std::string_view value);

QString variantString(const QVariantMap& map, const QString& key, const QString& fallback = QString{});
bool variantBool(const QVariantMap& map, const QString& key, bool fallback = false);
double variantDouble(const QVariantMap& map, const QString& key, double fallback = 0.0);

bool isPythonPipelineId(const QString& pipelineId);
QJsonDocument runPythonCodecCli(const QStringList& args, int timeoutMs = 120000);
QVariantList pythonCodecPipelineRows();
QVariantMap findPythonPipelineRow(const QString& pipelineId);

const hft_compressor::PipelineDescriptor* findPipeline(const QString& pipelineId);
hft_compressor::StreamType streamTypeForScope(const QString& scope);
hft_compressor::CompressionResult failedCppCompressionResult(const QVariantMap& row,
                                                             const QString& input,
                                                             const QString& outputPath,
                                                             const QString& error);

QString pipelineSummary(const hft_compressor::PipelineDescriptor& pipeline);
QString pipelineLabelFor(const QString& pipelineId);
QString pipelineFileExtensionFor(const QString& pipelineId);
QString pythonOutputSlugFor(const QString& pipelineId);
QString pipelineOutputSlugFor(const QString& pipelineId);
QString sessionIdForInputPath(const QString& inputPath);
double mbpsValue(std::uint64_t bytes, std::uint64_t ns);
QString artifactPathFromMetricsPath(const QString& metricsPath, const QString& pipelineId, const QString& metricsSuffix);
QString pathFromUrl(const QUrl& url);
QString resolveRecordingsRoot();
QString sessionSourceSummary(const BacktestLegCounts& backtestCounts, const QString& sessionPath);
QString displayChannel(const QString& channel);
QString displayProfile(const QString& profile);
QString normalizedPipelineId(const QJsonObject& object);
QString normalizedInputPath(const QJsonObject& object);
QString normalizedOutputPath(const QJsonObject& object);
QString groupForPipeline(const hft_compressor::PipelineDescriptor& pipeline);
bool isCompressTablePipeline(const hft_compressor::PipelineDescriptor& pipeline);
bool pipelineMatchesChannel(const hft_compressor::PipelineDescriptor& pipeline, const QString& channel);
std::uint64_t jsonUInt64(const QJsonObject& object, const char* key);
QString jsonString(const QJsonObject& object, const char* key);

QVariantMap previewError(const QString& path, const QString& error);
QVariantMap previewOkBase(const QFileInfo& info, const QString& kind);
QString hexDumpText(const QByteArray& bytes);
QVariantMap metadataRow(const QString& label, const QString& value);

}  // namespace hftrec::gui::compression_vm
