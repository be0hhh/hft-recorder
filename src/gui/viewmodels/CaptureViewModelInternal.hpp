#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <cstdint>
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
    qulonglong candles2Count{0};
    qulonglong depthCount{0};
};

QStringList loadAliasesForChannel(const char* channelName);
QStringList normalizedSelectedAliasesForChannel(const QString& channel, const QStringList& selectedAliases);
QStringList requiredAliasesForChannel(const QString& channel);
bool isRequiredAliasForChannel(const QString& channel, const QString& alias);
int aliasWeightBytes(const QString& alias);
QString channelWeightSummary(const QString& channel, const QStringList& selectedAliases);
QVariantList venueChoices();
QVariantList detailedCandlesVenueChoices();
QString buildRequestPreview(const QString& channel,
                            const QStringList& availableAliases,
                            const QStringList& selectedAliases,
                            const QStringList& venueKeys,
                            const QStringList& venueSymbolsTexts,
                            const QString& symbolsText,
                             int apiSlot);
std::vector<std::string> normalizedSymbols(const QString& symbolsText);
QString venueSymbolsFromGlobalInput(const QString& venueKey, const QString& symbolsText);
QString venueSymbolPlaceholder(const QString& venueKey);
QString missingVenueSymbolsText(const QStringList& venueKeys, const QStringList& venueSymbolsTexts);
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
std::vector<capture::CaptureConfig> makeDetailedCandlesConfigs(const QString& outputDirectory,
                                                               const QString& envPath,
                                                               int apiSlot,
                                                               const QString& venueKey,
                                                               const QString& symbolText,
                                                               const QString& timeframe,
                                                               int limit,
                                                               QString* errorText,
                                                               std::int64_t endNs = 0);
std::vector<capture::CaptureConfig> makeDetailedCandlesConfigs(const QString& outputDirectory,
                                                               const QString& envPath,
                                                               int apiSlot,
                                                               const QString& leg1VenueKey,
                                                               const QString& leg1SymbolText,
                                                               const QString& leg2VenueKey,
                                                               const QString& leg2SymbolText,
                                                               const QString& timeframe,
                                                               int limit,
                                                               QString* errorText,
                                                               std::int64_t endNs = 0);
QVariantList detailedCandlesEndModeChoices();
std::int64_t parseDetailedCandlesEndUtcText(const QString& text, QString* errorText);
std::vector<std::int64_t> detailedCandlesEndCandidatesNs(const QString& mode,
                                                         const QString& manualUtcText,
                                                         const QString& leg1VenueKey,
                                                         const QString& leg2VenueKey,
                                                         std::int64_t nowNs,
                                                         QString* resolvedText,
                                                         QString* errorText);
QVariantList detailedCandlesSymbolSuggestions(const QString& venueKey,
                                              const QString& query,
                                              const QString& anchorVenueKey,
                                              const QString& anchorSymbolText);
QVariantList detailedCandlesTimeframeChoices(const QString& venueKey);
QString defaultDetailedCandlesTimeframe(const QString& venueKey);
QString detailedCandlesLimitHint(const QString& venueKey,
                                 const QString& timeframe);
QString detailedCandlesLimitWarning(const QString& venueKey,
                                    const QString& timeframe,
                                    int limit);
QString buildDetailedCandlesPreview(const QString& venueKey,
                                    const QString& symbolText,
                                    const QString& timeframe,
                                    int limit);
QString buildDetailedCandlesPreview(const QString& leg1VenueKey,
                                    const QString& leg1SymbolText,
                                    const QString& leg2VenueKey,
                                    const QString& leg2SymbolText,
                                    const QString& timeframe,
                                    int limit);
CaptureBatchSnapshot collectBatchSnapshot(const CaptureViewModel& viewModel, CaptureRefreshMode mode);

}  // namespace detail
}  // namespace hftrec::gui
