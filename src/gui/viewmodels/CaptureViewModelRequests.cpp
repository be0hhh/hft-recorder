#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#include <QRegularExpression>
#include <QVariantMap>
#include <algorithm>
#include <cstdint>
#include <filesystem>

namespace hftrec::gui::detail {

namespace {

constexpr int kDetailedCandlesMaxLimit = 1'000'000;

QString normalizeToken(QString token) {
    return token.trimmed();
}

struct VenueSpec {
    const char* key;
    const char* label;
    const char* exchange;
    const char* market;
};

constexpr VenueSpec kVenues[] = {
    {"binance_futures", "Binance Futures", "binance", "futures"},
    {"binance_spot", "Binance Spot", "binance", "spot"},
    {"bybit_futures", "Bybit Futures", "bybit", "futures"},
    {"bybit_spot", "Bybit Spot", "bybit", "spot"},
    {"kucoin_futures", "KuCoin Futures", "kucoin", "futures"},
    {"kucoin_spot", "KuCoin Spot", "kucoin", "spot"},
    {"gate_futures", "Gate Futures", "gate", "futures"},
    {"gate_spot", "Gate Spot", "gate", "spot"},
    {"bitget_futures", "Bitget Futures", "bitget", "futures"},
    {"bitget_spot", "Bitget Spot", "bitget", "spot"},
    {"aster_futures", "Aster Futures", "aster", "futures"},
    {"aster_spot", "Aster Spot", "aster", "spot"},
    {"okx_futures", "OKX Futures", "okx", "futures"},
    {"okx_spot", "OKX Spot", "okx", "spot"},
    {"finam_futures", "FINAM Futures", "finam", "futures"},
    {"finam_spot", "FINAM Spot", "finam", "spot"},
    {"mexc_spot", "MEXC Spot", "mexc", "spot"},
};

std::vector<VenueSpec> selectedVenues(const QStringList& venueKeys) {
    std::vector<VenueSpec> venues;
    for (const auto& rawKey : venueKeys) {
        const auto key = rawKey.trimmed().toLower();
        for (const auto& venue : kVenues) {
            if (key == QString::fromLatin1(venue.key)) {
                venues.push_back(venue);
                break;
            }
        }
    }
    if (venues.empty()) venues.push_back(kVenues[0]);
    return venues;
}

qsizetype venueIndex(const QString& venueKey) noexcept {
    const auto key = venueKey.trimmed().toLower();
    for (qsizetype i = 0; i < static_cast<qsizetype>(std::size(kVenues)); ++i) {
        if (key == QString::fromLatin1(kVenues[i].key)) return i;
    }
    return -1;
}

const VenueSpec* venueByKey(const QString& venueKey) noexcept {
    const qsizetype idx = venueIndex(venueKey);
    return idx >= 0 ? &kVenues[idx] : nullptr;
}

QString symbolsTextForVenue(const VenueSpec& venue,
                            const QStringList& venueSymbolsTexts,
                            const QString& fallbackSymbolsText) {
    const qsizetype idx = venueIndex(QString::fromLatin1(venue.key));
    if (idx >= 0 && idx < venueSymbolsTexts.size()) {
        return venueSymbolsTexts[idx].trimmed();
    }
    return venueSymbolsFromGlobalInput(QString::fromLatin1(venue.key), fallbackSymbolsText);
}

bool isFinamVenue(const VenueSpec& venue) noexcept {
    return venue.exchange[0] == 'f' && venue.exchange[1] == 'i' && venue.exchange[2] == 'n' &&
           venue.exchange[3] == 'a' && venue.exchange[4] == 'm';
}

bool supportsDetailedCandlesVenue(const VenueSpec& venue) noexcept {
    return !(venue.exchange[0] == 'm' && venue.exchange[1] == 'e' &&
             venue.exchange[2] == 'x' && venue.exchange[3] == 'c');
}

QString normalizeDetailedTimeframe(QString timeframe) {
    timeframe = timeframe.trimmed();
    if (timeframe == QStringLiteral("1M")) return timeframe;
    return timeframe.toLower();
}

QStringList detailedCandlesTimeframesForVenue(const VenueSpec& venue) {
    if (isFinamVenue(venue)) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("2h"),
            QStringLiteral("4h"),
            QStringLiteral("8h"),
            QStringLiteral("1d"),
            QStringLiteral("1w"),
            QStringLiteral("1M"),
        };
    }
    const QString exchange = QString::fromLatin1(venue.exchange);
    if (exchange == QStringLiteral("okx")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("3m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("2h"),
            QStringLiteral("4h"),
            QStringLiteral("6h"),
            QStringLiteral("12h"),
            QStringLiteral("1d"),
            QStringLiteral("1w"),
            QStringLiteral("1M"),
        };
    }
    if (exchange == QStringLiteral("kucoin")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("15m"),
            QStringLiteral("1h"),
            QStringLiteral("4h"),
            QStringLiteral("1d"),
        };
    }
    if (exchange == QStringLiteral("bitget")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("3m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("4h"),
            QStringLiteral("6h"),
            QStringLiteral("12h"),
            QStringLiteral("1d"),
        };
    }
    if (exchange == QStringLiteral("gate")) {
        return {
            QStringLiteral("1m"),
            QStringLiteral("5m"),
            QStringLiteral("15m"),
            QStringLiteral("30m"),
            QStringLiteral("1h"),
            QStringLiteral("4h"),
            QStringLiteral("8h"),
            QStringLiteral("1d"),
            QStringLiteral("7d"),
            QStringLiteral("30d"),
        };
    }
    return {
        QStringLiteral("1m"),
        QStringLiteral("3m"),
        QStringLiteral("5m"),
        QStringLiteral("15m"),
        QStringLiteral("30m"),
        QStringLiteral("1h"),
        QStringLiteral("2h"),
        QStringLiteral("4h"),
        QStringLiteral("6h"),
        QStringLiteral("8h"),
        QStringLiteral("12h"),
        QStringLiteral("1d"),
        QStringLiteral("3d"),
        QStringLiteral("1w"),
        QStringLiteral("1M"),
    };
}

bool detailedCandlesSupportsTimeframe(const VenueSpec& venue, const QString& timeframe) {
    return detailedCandlesTimeframesForVenue(venue).contains(timeframe);
}

QString detailedCandlesTimeframeSummary(const VenueSpec& venue) {
    return detailedCandlesTimeframesForVenue(venue).join(QStringLiteral(", "));
}

QString detailedCandlesTimeframeForVenue(const VenueSpec& venue, const QString& requestedTimeframe) {
    (void)venue;
    return normalizeDetailedTimeframe(requestedTimeframe);
}

QString marketDsl(const VenueSpec& venue) {
    const QString market = QString::fromLatin1(venue.market);
    if (market == QStringLiteral("spot")) return QStringLiteral("spot");
    if (market == QStringLiteral("margin")) return QStringLiteral("margin");
    if (market == QStringLiteral("inverse")) return QStringLiteral("inverse");
    if (market == QStringLiteral("swap")) return QStringLiteral("swap");
    return QStringLiteral("futures");
}

QString toDslSymbol(const VenueSpec& venue, const std::string& symbol) {
    QString value = QString::fromStdString(symbol).trimmed();
    return isFinamVenue(venue) ? value : value.toLower();
}

struct ParsedSymbol {
    QString base;
    QString quote;
};

ParsedSymbol parseGlobalSymbol(QString symbol) {
    symbol = symbol.trimmed().toUpper();
    symbol.remove(QLatin1Char('-'));
    symbol.remove(QLatin1Char('_'));
    if (symbol.endsWith(QStringLiteral("SWAP"))) symbol.chop(4);

    static const QStringList quotes{
        QStringLiteral("USDT"),
        QStringLiteral("USDC"),
        QStringLiteral("USD"),
    };
    for (const auto& quote : quotes) {
        if (symbol.size() > quote.size() && symbol.endsWith(quote)) {
            symbol.chop(quote.size());
            return {symbol, quote};
        }
    }
    return {symbol, QStringLiteral("USDT")};
}

QString formattedVenueSymbol(const VenueSpec& venue, const ParsedSymbol& symbol) {
    const QString exchange = QString::fromLatin1(venue.exchange);
    const QString market = QString::fromLatin1(venue.market);
    QString base = symbol.base;
    QString quote = symbol.quote;

    if (exchange == QStringLiteral("kucoin") && market == QStringLiteral("futures")
        && base == QStringLiteral("BTC")) {
        base = QStringLiteral("XBT");
    }
    if (market == QStringLiteral("inverse")) quote = QStringLiteral("USD");
    if (exchange == QStringLiteral("bitget") && market == QStringLiteral("swap")) {
        quote = QStringLiteral("USDC");
    }

    if (exchange == QStringLiteral("kucoin")) {
        if (market == QStringLiteral("futures")) return base + quote + QStringLiteral("M");
        return base + QLatin1Char('-') + quote;
    }
    if (exchange == QStringLiteral("gate")) return base + QLatin1Char('_') + quote;
    if (exchange == QStringLiteral("okx")) {
        const QString result = base + QLatin1Char('-') + quote;
        return market == QStringLiteral("futures") ? result + QStringLiteral("-SWAP") : result;
    }
    if (exchange == QStringLiteral("binance") && market == QStringLiteral("inverse")) {
        return base + quote + QStringLiteral("_PERP");
    }
    return base + quote;
}

bool containsAlias(const QStringList& aliases, const QString& alias) {
    return aliases.contains(alias, Qt::CaseInsensitive);
}

QStringList canonicalTradesAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("id"),
        QStringLiteral("isBuyerMaker"),
        QStringLiteral("firstTradeId"),
        QStringLiteral("lastTradeId"),
        QStringLiteral("quoteQty"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
    };
}

QStringList canonicalLiquidationsAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("avgPrice"),
        QStringLiteral("filledQty"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
        QStringLiteral("orderType"),
        QStringLiteral("timeInForce"),
        QStringLiteral("status"),
        QStringLiteral("sourceMode"),
        QStringLiteral("captureSeq"),
        QStringLiteral("ingestSeq"),
    };
}
QStringList canonicalBookTickerAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
    };
}

QStringList canonicalOrderbookAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
    };
}

QStringList requiredTradesAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
    };
}

QStringList requiredLiquidationsAliases() {
    return {
        QStringLiteral("price"),
        QStringLiteral("amount"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
        QStringLiteral("avgPrice"),
        QStringLiteral("filledQty"),
    };
}
QStringList requiredBookTickerAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("timestamp"),
    };
}

QStringList requiredOrderbookAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("side"),
        QStringLiteral("timestamp"),
    };
}

QString channelObjectName(const QString& channel) {
    if (channel == QStringLiteral("liquidations")) return QStringLiteral("liquidation");
    return channel;
}
QString buildAliasesSuffix(const QStringList& availableAliases,
                           const QStringList& selectedAliases) {
    QStringList orderedSelected;
    orderedSelected.reserve(selectedAliases.size());
    for (const auto& alias : availableAliases) {
        if (selectedAliases.contains(alias, Qt::CaseInsensitive)) orderedSelected.push_back(alias);
    }
    if (orderedSelected.isEmpty()) return {};
    return QStringLiteral(".aliases(%1)").arg(orderedSelected.join(QStringLiteral(",")));
}

int normalizedApiSlot(int apiSlot) noexcept {
    if (apiSlot < 1) return 1;
    if (apiSlot > 255) return 255;
    return apiSlot;
}

}  // namespace

QStringList loadAliasesForChannel(const char* channelName) {
    const auto channel = QString::fromUtf8(channelName);
    if (channel == QStringLiteral("trades")) return canonicalTradesAliases();
    if (channel == QStringLiteral("liquidations")) return canonicalLiquidationsAliases();
    if (channel == QStringLiteral("bookticker")) return canonicalBookTickerAliases();
    if (channel == QStringLiteral("orderbook")) return canonicalOrderbookAliases();
    return {};
}

QVariantList venueChoices() {
    QVariantList choices;
    for (const auto& venue : kVenues) {
        QVariantMap item;
        item.insert(QStringLiteral("key"), QString::fromLatin1(venue.key));
        item.insert(QStringLiteral("label"), QString::fromLatin1(venue.label));
        choices.push_back(item);
    }
    return choices;
}

QVariantList detailedCandlesVenueChoices() {
    QVariantList choices;
    for (const auto& venue : kVenues) {
        if (!supportsDetailedCandlesVenue(venue)) continue;
        QVariantMap item;
        item.insert(QStringLiteral("key"), QString::fromLatin1(venue.key));
        item.insert(QStringLiteral("label"), QString::fromLatin1(venue.label));
        choices.push_back(item);
    }
    return choices;
}

QStringList requiredAliasesForChannel(const QString& channel) {
    if (channel == QStringLiteral("trades")) return requiredTradesAliases();
    if (channel == QStringLiteral("liquidations")) return requiredLiquidationsAliases();
    if (channel == QStringLiteral("bookticker")) return requiredBookTickerAliases();
    if (channel == QStringLiteral("orderbook")) return requiredOrderbookAliases();
    return {};
}

bool isRequiredAliasForChannel(const QString& channel, const QString& alias) {
    return containsAlias(requiredAliasesForChannel(channel), alias);
}

QStringList normalizedSelectedAliasesForChannel(const QString& channel,
                                                const QStringList& selectedAliases) {
    QStringList normalized = selectedAliases;
    for (const auto& alias : requiredAliasesForChannel(channel)) {
        if (!normalized.contains(alias, Qt::CaseInsensitive)) normalized.push_back(alias);
    }
    return normalized;
}

int aliasWeightBytes(const QString& alias) {
    if (alias == QStringLiteral("price") || alias == QStringLiteral("amount") ||
        alias == QStringLiteral("quoteQty") || alias == QStringLiteral("avgPrice") ||
        alias == QStringLiteral("filledQty") || alias == QStringLiteral("timestamp") ||
        alias == QStringLiteral("id") || alias == QStringLiteral("firstTradeId") ||
        alias == QStringLiteral("lastTradeId") || alias == QStringLiteral("captureSeq") ||
        alias == QStringLiteral("ingestSeq") || alias == QStringLiteral("updateId") ||
        alias == QStringLiteral("bidPrice") || alias == QStringLiteral("bidQty") ||
        alias == QStringLiteral("askPrice") || alias == QStringLiteral("askQty")) {
        return 8;
    }
    if (alias == QStringLiteral("side") || alias == QStringLiteral("isBuyerMaker") ||
        alias == QStringLiteral("orderType") || alias == QStringLiteral("timeInForce") ||
        alias == QStringLiteral("status") || alias == QStringLiteral("sourceMode") ||
        alias == QStringLiteral("exchange") || alias == QStringLiteral("market")) {
        return 1;
    }
    if (alias == QStringLiteral("symbol")) return 32;
    return 0;
}

QString channelWeightSummary(const QString& channel, const QStringList& selectedAliases) {
    const auto normalizedAliases = normalizedSelectedAliasesForChannel(channel, selectedAliases);
    if (channel == QStringLiteral("orderbook")) {
        int baseBytes = 0;
        int perBidBytes = 0;
        int perAskBytes = 0;
        for (const auto& alias : normalizedAliases) {
            if (alias == QStringLiteral("timestamp") || alias == QStringLiteral("symbol") ||
                alias == QStringLiteral("exchange") || alias == QStringLiteral("market")) {
                baseBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("side")) {
                perBidBytes += aliasWeightBytes(alias);
                perAskBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("bidPrice") || alias == QStringLiteral("bidQty")) {
                perBidBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("askPrice") || alias == QStringLiteral("askQty")) {
                perAskBytes += aliasWeightBytes(alias);
            }
        }
        return QStringLiteral("Total ") + QString::number(baseBytes)
            + QStringLiteral("B + N_bid*") + QString::number(perBidBytes)
            + QStringLiteral("B + N_ask*") + QString::number(perAskBytes)
            + QStringLiteral("B");
    }

    int totalBytes = 0;
    for (const auto& alias : normalizedAliases) totalBytes += aliasWeightBytes(alias);
    return QStringLiteral("Total ") + QString::number(totalBytes) + QStringLiteral("B");
}

std::vector<std::string> normalizedSymbols(const QString& symbolsText) {
    std::vector<std::string> symbols;
    const auto rawTokens = symbolsText.split(QRegularExpression(QStringLiteral("[,\\s]+")), Qt::SkipEmptyParts);
    symbols.reserve(rawTokens.size());
    for (const auto& rawToken : rawTokens) {
        const auto normalized = normalizeToken(rawToken);
        if (normalized.isEmpty()) continue;
        const auto asStd = normalized.toStdString();
        if (std::find(symbols.begin(), symbols.end(), asStd) == symbols.end()) symbols.push_back(asStd);
    }
    return symbols;
}

QString venueSymbolsFromGlobalInput(const QString& venueKey, const QString& symbolsText) {
    const qsizetype idx = venueIndex(venueKey);
    if (idx < 0) return {};

    QStringList formatted;
    const auto symbols = normalizedSymbols(symbolsText);
    formatted.reserve(static_cast<qsizetype>(symbols.size()));
    if (isFinamVenue(kVenues[idx])) {
        for (const auto& symbol : symbols) {
            const QString value = QString::fromStdString(symbol).trimmed();
            if (!value.isEmpty()) formatted.push_back(value);
        }
        return formatted.join(QLatin1Char('\n'));
    }
    for (const auto& symbol : symbols) {
        const auto parsed = parseGlobalSymbol(QString::fromStdString(symbol));
        if (!parsed.base.isEmpty()) formatted.push_back(formattedVenueSymbol(kVenues[idx], parsed));
    }
    return formatted.join(QLatin1Char('\n'));
}

QString venueSymbolPlaceholder(const QString& venueKey) {
    const qsizetype idx = venueIndex(venueKey);
    if (idx < 0) return QStringLiteral("Example: BTCUSDT");
    if (isFinamVenue(kVenues[idx])) return QStringLiteral("Example: SBER@MISX");
    return QStringLiteral("Example: %1").arg(formattedVenueSymbol(kVenues[idx], parseGlobalSymbol(QStringLiteral("BTCUSDT"))));
}

QString venueSymbolExample(const VenueSpec& venue) {
    if (isFinamVenue(venue)) return QStringLiteral("SBER@MISX");
    return formattedVenueSymbol(venue, parseGlobalSymbol(QStringLiteral("BTCUSDT")));
}

QString missingVenueSymbolsText(const QStringList& venueKeys, const QStringList& venueSymbolsTexts) {
    QStringList missing;
    for (const auto& rawKey : venueKeys) {
        const auto key = rawKey.trimmed().toLower();
        const qsizetype idx = venueIndex(key);
        if (idx < 0) continue;
        if (idx < venueSymbolsTexts.size() && !venueSymbolsTexts[idx].trimmed().isEmpty()) continue;
        const auto label = QString::fromLatin1(kVenues[idx].label);
        const auto example = venueSymbolExample(kVenues[idx]);
        missing.push_back(QStringLiteral("%1 symbol is empty; expected %2").arg(label, example));
    }
    return missing.join(QStringLiteral(" | "));
}

QString buildRequestPreview(const QString& channel,
                            const QStringList& availableAliases,
                            const QStringList& selectedAliases,
                            const QStringList& venueKeys,
                            const QStringList& venueSymbolsTexts,
                            const QString& symbolsText,
                            int apiSlot) {
    const auto normalizedSelectedAliases = normalizedSelectedAliasesForChannel(channel, selectedAliases);
    const auto aliasesSuffix = buildAliasesSuffix(availableAliases, normalizedSelectedAliases);
    const auto objectName = channelObjectName(channel);
    const auto apiSuffix = QStringLiteral(".api(%1)").arg(normalizedApiSlot(apiSlot));

    const auto venues = selectedVenues(venueKeys);

    QStringList commands;
    for (const auto& venue : venues) {
        const auto symbols = normalizedSymbols(symbolsTextForVenue(venue, venueSymbolsTexts, symbolsText));
        if (symbols.empty()) {
            commands.push_back(QStringLiteral("subscribe().object(%1).exchange(%2).market(%3)%4.symbol(<symbol>)%5")
                                   .arg(objectName, QString::fromLatin1(venue.exchange), marketDsl(venue), apiSuffix, aliasesSuffix));
            continue;
        }
        for (const auto& symbol : symbols) {
            commands.push_back(
                QStringLiteral("subscribe().object(%1).exchange(%2).market(%3)%4.symbol(%5)%6")
                    .arg(objectName,
                         QString::fromLatin1(venue.exchange),
                         marketDsl(venue),
                         apiSuffix,
                         toDslSymbol(venue, symbol),
                         aliasesSuffix));
        }
    }
    return commands.join(QStringLiteral("\n"));
}

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
                                                int tradesHistoryWarmupSec) {
    std::vector<capture::CaptureConfig> configs;
    const auto venues = selectedVenues(venueKeys);

    const auto normalizedTradesAliases = normalizedSelectedAliasesForChannel(QStringLiteral("trades"), selectedTradesAliases);
    const auto normalizedLiquidationsAliases = normalizedSelectedAliasesForChannel(QStringLiteral("liquidations"), selectedLiquidationsAliases);
    const auto normalizedBookTickerAliases = normalizedSelectedAliasesForChannel(QStringLiteral("bookticker"), selectedBookTickerAliases);
    const auto normalizedOrderbookAliases = normalizedSelectedAliasesForChannel(QStringLiteral("orderbook"), selectedOrderbookAliases);
    const auto tradesSuffix = buildAliasesSuffix(tradesAvailableAliases, normalizedTradesAliases);
    const auto liquidationsSuffix = buildAliasesSuffix(liquidationsAvailableAliases, normalizedLiquidationsAliases);
    const auto bookTickerSuffix = buildAliasesSuffix(bookTickerAvailableAliases, normalizedBookTickerAliases);
    const auto orderbookSuffix = buildAliasesSuffix(orderbookAvailableAliases, normalizedOrderbookAliases);

    for (const auto& venue : venues) {
        const auto symbols = normalizedSymbols(symbolsTextForVenue(venue, venueSymbolsTexts, symbolsText));
        for (const auto& symbol : symbols) {
            capture::CaptureConfig config{};
            config.exchange = venue.exchange;
            config.market = venue.market;
            config.symbols = {symbol};
            config.envPath = std::filesystem::path{envPath.toStdString()};
            config.apiSlot = static_cast<std::uint8_t>(normalizedApiSlot(apiSlot));
            config.outputDir = std::filesystem::path{outputDirectory.toStdString()};
            config.durationSec = 0;
            config.snapshotIntervalSec = 60;
            int effectiveTradesHistoryWarmupSec = tradesHistoryWarmupSec;
            if ((venue.exchange[0] == 'g' || venue.exchange[0] == 'k') && effectiveTradesHistoryWarmupSec > 60) {
                effectiveTradesHistoryWarmupSec = 60;
            }
            config.tradesHistoryWarmupSec = effectiveTradesHistoryWarmupSec;

            const auto symbolDsl = toDslSymbol(venue, symbol);
            config.tradesAliases.reserve(static_cast<std::size_t>(normalizedTradesAliases.size()));
            for (const auto& alias : normalizedTradesAliases) config.tradesAliases.push_back(alias.toStdString());
            config.liquidationAliases.reserve(static_cast<std::size_t>(normalizedLiquidationsAliases.size()));
            for (const auto& alias : normalizedLiquidationsAliases) config.liquidationAliases.push_back(alias.toStdString());
            config.bookTickerAliases.reserve(static_cast<std::size_t>(normalizedBookTickerAliases.size()));
            for (const auto& alias : normalizedBookTickerAliases) config.bookTickerAliases.push_back(alias.toStdString());
            config.orderbookAliases.reserve(static_cast<std::size_t>(normalizedOrderbookAliases.size()));
            for (const auto& alias : normalizedOrderbookAliases) config.orderbookAliases.push_back(alias.toStdString());

            const auto exchangeDsl = QString::fromLatin1(venue.exchange);
            const auto market = marketDsl(venue);
            const auto apiSlotText = QString::number(normalizedApiSlot(apiSlot));
            config.tradesRequestCommand =
                QStringLiteral("subscribe().object(trades).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, tradesSuffix)
                    .toStdString();
            config.liquidationRequestCommand =
                QStringLiteral("subscribe().object(liquidation).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, liquidationsSuffix)
                    .toStdString();
            config.bookTickerRequestCommand =
                QStringLiteral("subscribe().object(bookticker).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, bookTickerSuffix)
                    .toStdString();
            config.orderbookRequestCommand =
                QStringLiteral("subscribe().object(orderbook).exchange(%1).market(%2).api(%3).symbol(%4)%5")
                    .arg(exchangeDsl, market, apiSlotText, symbolDsl, orderbookSuffix)
                    .toStdString();
            configs.push_back(std::move(config));
        }
    }

    return configs;
}

std::vector<capture::CaptureConfig> makeDetailedCandlesConfigs(const QString& outputDirectory,
                                                               const QString& envPath,
                                                               int apiSlot,
                                                               const QString& venueKey,
                                                               const QString& symbolText,
                                                               const QString& timeframe,
                                                               int limit,
                                                               QString* errorText) {
    if (errorText != nullptr) errorText->clear();
    std::vector<capture::CaptureConfig> configs;
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) {
        if (errorText != nullptr) *errorText = QStringLiteral("Select detailed candles venue");
        return configs;
    }
    if (!supportsDetailedCandlesVenue(*venue)) {
        if (errorText != nullptr) *errorText = QStringLiteral("Selected venue does not support detailed candles");
        return configs;
    }

    const QString tf = detailedCandlesTimeframeForVenue(*venue, timeframe);
    if (tf.isEmpty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter detailed candles timeframe");
        return configs;
    }
    if (!detailedCandlesSupportsTimeframe(*venue, tf)) {
        if (errorText != nullptr) {
            *errorText = QStringLiteral("%1 candles support %2; got %3")
                .arg(QString::fromLatin1(venue->label), detailedCandlesTimeframeSummary(*venue), tf);
        }
        return configs;
    }

    const auto symbols = normalizedSymbols(symbolText);
    if (symbols.empty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter detailed candles symbol");
        return configs;
    }
    if (symbols.size() != 1u) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter one detailed candles symbol");
        return configs;
    }
    const std::uint32_t clampedLimit = static_cast<std::uint32_t>(std::clamp(limit, 1, kDetailedCandlesMaxLimit));
    capture::CaptureConfig config{};
    config.exchange = venue->exchange;
    config.market = venue->market;
    config.symbols = {symbols.front()};
    config.envPath = std::filesystem::path{envPath.toStdString()};
    config.apiSlot = static_cast<std::uint8_t>(normalizedApiSlot(apiSlot));
    config.outputDir = std::filesystem::path{outputDirectory.toStdString()};
    config.detailedCandlesTimeframe = tf.toStdString();
    config.detailedCandlesLimit = clampedLimit;
    config.detailedCandlesEndNs = 0;
    configs.push_back(std::move(config));
    return configs;
}

QVariantList detailedCandlesTimeframeChoices(const QString& venueKey) {
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) venue = &kVenues[0];
    QVariantList choices;
    const auto timeframes = detailedCandlesTimeframesForVenue(*venue);
    for (const auto& timeframe : timeframes) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), timeframe);
        item.insert(QStringLiteral("value"), timeframe);
        if (isFinamVenue(*venue)) {
            item.insert(QStringLiteral("rightText"), QStringLiteral("FINAM bars"));
        } else {
            item.insert(QStringLiteral("rightText"), QStringLiteral("REST pages"));
        }
        choices.push_back(item);
    }
    return choices;
}

QString defaultDetailedCandlesTimeframe(const QString& venueKey) {
    const auto* venue = venueByKey(venueKey);
    if (venue == nullptr) venue = &kVenues[0];
    const auto timeframes = detailedCandlesTimeframesForVenue(*venue);
    return timeframes.isEmpty() ? QStringLiteral("1m") : timeframes.front();
}

QString buildDetailedCandlesPreview(const QString& venueKey,
                                    const QString& symbolText,
                                    const QString& timeframe,
                                    int limit) {
    const auto* venue = venueByKey(venueKey);
    const QString tf = venue == nullptr ? normalizeDetailedTimeframe(timeframe) : detailedCandlesTimeframeForVenue(*venue, timeframe);
    const QString limitText = QString::number(std::clamp(limit, 1, kDetailedCandlesMaxLimit));
    if (venue == nullptr) {
        return QStringLiteral("get().object(klines).exchange(<venue>).market(<market>).timeframe(%1).limit(%2).symbol(<symbol>) -> jsonl/candles2_%1.jsonl")
            .arg(tf, limitText);
    }
    const auto symbols = normalizedSymbols(symbolText);
    const QString exchange = QString::fromLatin1(venue->exchange);
    const QString market = marketDsl(*venue);
    const QString path = QStringLiteral("jsonl/candles2_%1.jsonl").arg(tf);
    const QString symbol = symbols.empty()
        ? QStringLiteral("<symbol>")
        : toDslSymbol(*venue, symbols.front());
    QString command = QStringLiteral("get().object(klines).exchange(%1).market(%2).timeframe(%3).limit(%4).symbol(%5)")
        .arg(exchange, market, tf, limitText, symbol);
    return command + QStringLiteral(" -> ") + path;
}

}  // namespace hftrec::gui::detail
