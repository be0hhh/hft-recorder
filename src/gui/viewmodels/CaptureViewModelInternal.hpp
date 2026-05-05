#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::gui {

class CaptureViewModel;

namespace detail {

struct CaptureBatchSnapshot {
    QString sessionId{};
    QString sessionPath{};
    QString errorText{};
    bool tradesRunning{false};
    bool liquidationsRunning{false};
    bool bookTickerRunning{false};
    bool orderbookRunning{false};
    qulonglong tradesCount{0};
    qulonglong liquidationsCount{0};
    qulonglong bookTickerCount{0};
    qulonglong depthCount{0};
};

QStringList loadAliasesForChannel(const char* channelName);
QStringList normalizedSelectedAliasesForChannel(const QString& channel, const QStringList& selectedAliases);
QStringList requiredAliasesForChannel(const QString& channel);
bool isRequiredAliasForChannel(const QString& channel, const QString& alias);
int aliasWeightBytes(const QString& alias);
QString channelWeightSummary(const QString& channel, const QStringList& selectedAliases);
QVariantList venueChoices();
QString buildRequestPreview(const QString& channel,
                            const QStringList& availableAliases,
                            const QStringList& selectedAliases,
                            const QStringList& venueKeys,
                            const QStringList& venueSymbolsTexts,
                            const QString& symbolsText);
std::vector<std::string> normalizedSymbols(const QString& symbolsText);
std::vector<capture::CaptureConfig> makeConfigs(const QString& outputDirectory,
                                                const QStringList& venueKeys,
                                                const QStringList& venueSymbolsTexts,
                                                const QString& symbolsText,
                                                const QStringList& tradesAvailableAliases,
                                                const QStringList& liquidationsAvailableAliases,
                                                const QStringList& bookTickerAvailableAliases,
                                                const QStringList& orderbookAvailableAliases,
                                                const QStringList& selectedTradesAliases,
                                                const QStringList& selectedLiquidationsAliases,
                                                const QStringList& selectedBookTickerAliases,
                                                const QStringList& selectedOrderbookAliases);
CaptureBatchSnapshot collectBatchSnapshot(const CaptureViewModel& viewModel);

}  // namespace detail
}  // namespace hftrec::gui
