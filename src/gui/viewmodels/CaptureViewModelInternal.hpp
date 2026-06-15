#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::gui {

class CaptureViewModel;

namespace detail {

enum class CaptureRefreshMode {
    Light,
    Full,
};

struct CaptureBatchSnapshot {
    QString sessionId{};
    QString sessionPath{};
    QString errorText{};
    bool tradesRunning{false};
    bool liquidationsRunning{false};
    bool bookTickerRunning{false};
    bool orderbookRunning{false};
    bool markPriceRunning{false};
    bool indexPriceRunning{false};
    bool fundingRunning{false};
    bool priceLimitRunning{false};
    qulonglong tradesCount{0};
    qulonglong liquidationsCount{0};
    qulonglong bookTickerCount{0};
    qulonglong markPriceCount{0};
    qulonglong indexPriceCount{0};
    qulonglong fundingCount{0};
    qulonglong priceLimitCount{0};
    qulonglong candlesCount{0};
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
                            const QString& symbolsText,
                            int apiSlot);
std::vector<std::string> normalizedSymbols(const QString& symbolsText);
QString venueSymbolsFromGlobalInput(const QString& venueKey, const QString& symbolsText);
std::vector<capture::CaptureConfig> makeConfigs(const QString& outputDirectory,
                                                const QString& envPath,
                                                int apiSlot,
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
                                                const QStringList& selectedOrderbookAliases,
                                                int tradesHistoryWarmupSec);
CaptureBatchSnapshot collectBatchSnapshot(const CaptureViewModel& viewModel, CaptureRefreshMode mode);

}  // namespace detail
}  // namespace hftrec::gui
