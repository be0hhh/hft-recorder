#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#include <QRegularExpression>
#include <QVariantMap>
#include <algorithm>
#include <filesystem>

namespace hftrec::gui::detail {

namespace {

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
    {"binance_futures", "Binance Futures", "binance", "futures_usd"},
    {"kucoin_futures", "KuCoin Futures", "kucoin", "futures_usd"},
    {"gate_usdt", "Gate USDT", "gate", "futures_usd"},
    {"bitget_futures", "Bitget Futures", "bitget", "futures_usd"},
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

QString symbolsTextForVenue(const VenueSpec& venue,
                            const QStringList& venueSymbolsTexts,
                            const QString& fallbackSymbolsText) {
    const qsizetype idx = venueIndex(QString::fromLatin1(venue.key));
    if (idx >= 0 && idx < venueSymbolsTexts.size()) {
        const auto text = venueSymbolsTexts[idx].trimmed();
        if (!text.isEmpty()) return text;
    }
    return fallbackSymbolsText;
}

QString marketDsl(const VenueSpec& venue) {
    return QString::fromLatin1(venue.market) == QStringLiteral("swap") ? QStringLiteral("swap") : QStringLiteral("futures");
}

QString toDslSymbol(const std::string& symbol) {
    return QString::fromStdString(symbol).toLower();
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

QString buildRequestPreview(const QString& channel,
                            const QStringList& availableAliases,
                            const QStringList& selectedAliases,
                            const QStringList& venueKeys,
                            const QStringList& venueSymbolsTexts,
                            const QString& symbolsText) {
    const auto normalizedSelectedAliases = normalizedSelectedAliasesForChannel(channel, selectedAliases);
    const auto aliasesSuffix = buildAliasesSuffix(availableAliases, normalizedSelectedAliases);
    const auto objectName = channelObjectName(channel);

    const auto venues = selectedVenues(venueKeys);

    QStringList commands;
    for (const auto& venue : venues) {
        const auto symbols = normalizedSymbols(symbolsTextForVenue(venue, venueSymbolsTexts, symbolsText));
        if (symbols.empty()) {
            commands.push_back(QStringLiteral("subscribe().object(%1).exchange(%2).market(%3).symbol(<symbol>)%4")
                                   .arg(objectName, QString::fromLatin1(venue.exchange), marketDsl(venue), aliasesSuffix));
            continue;
        }
        for (const auto& symbol : symbols) {
            commands.push_back(
                QStringLiteral("subscribe().object(%1).exchange(%2).market(%3).symbol(%4)%5")
                    .arg(objectName,
                         QString::fromLatin1(venue.exchange),
                         marketDsl(venue),
                         toDslSymbol(symbol),
                         aliasesSuffix));
        }
    }
    return commands.join(QStringLiteral("\n"));
}

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
                                                const QStringList& selectedOrderbookAliases) {
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
            config.outputDir = std::filesystem::path{outputDirectory.toStdString()};
            config.durationSec = 0;
            config.snapshotIntervalSec = 60;

            const auto symbolDsl = toDslSymbol(symbol);
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
            config.tradesRequestCommand =
                QStringLiteral("subscribe().object(trades).exchange(%1).market(%2).symbol(%3)%4")
                    .arg(exchangeDsl, market, symbolDsl, tradesSuffix)
                    .toStdString();
            config.liquidationRequestCommand =
                QStringLiteral("subscribe().object(liquidation).exchange(%1).market(%2).symbol(%3)%4")
                    .arg(exchangeDsl, market, symbolDsl, liquidationsSuffix)
                    .toStdString();
            config.bookTickerRequestCommand =
                QStringLiteral("subscribe().object(bookticker).exchange(%1).market(%2).symbol(%3)%4")
                    .arg(exchangeDsl, market, symbolDsl, bookTickerSuffix)
                    .toStdString();
            config.orderbookRequestCommand =
                QStringLiteral("subscribe().object(orderbook).exchange(%1).market(%2).symbol(%3)%4")
                    .arg(exchangeDsl, market, symbolDsl, orderbookSuffix)
                    .toStdString();
            configs.push_back(std::move(config));
        }
    }

    return configs;
}

}  // namespace hftrec::gui::detail
