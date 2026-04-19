#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <filesystem>

namespace hftrec::gui::detail {

namespace {

QString normalizeToken(QString token) {
    token = token.trimmed().toUpper();
    if (token.isEmpty()) return token;
    if (!token.endsWith(QStringLiteral("USDT"))) token += QStringLiteral("USDT");
    return token;
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

QStringList canonicalBookTickerAliases() {
    return {
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("timestamp"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
    };
}

QStringList canonicalOrderbookAliases() {
    return {
        QStringLiteral("updateId"),
        QStringLiteral("bidPrice"),
        QStringLiteral("bidQty"),
        QStringLiteral("askPrice"),
        QStringLiteral("askQty"),
        QStringLiteral("timestamp"),
        QStringLiteral("symbol"),
        QStringLiteral("exchange"),
        QStringLiteral("market"),
    };
}

QStringList orderedAliases(const QStringList& preferredAliases, const QStringList& discoveredAliases) {
    QStringList ordered;
    ordered.reserve(preferredAliases.size());
    for (const auto& alias : preferredAliases) {
        if (discoveredAliases.isEmpty() || discoveredAliases.contains(alias)) {
            ordered.push_back(alias);
        }
    }
    return ordered;
}

QString cxetAliasSourcePath() {
    QFileInfo sourceFileInfo(QString::fromUtf8(__FILE__));
    QDir dir = sourceFileInfo.absoluteDir();
    for (int i = 0; i < 6 && dir.cdUp(); ++i) {
    }
    return dir.filePath(QStringLiteral("src/src/api/fields/RequestedFieldNames.cpp"));
}

QStringList extractAliasesFromCxetSource() {
    QFile sourceFile(cxetAliasSourcePath());
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QString content = QString::fromUtf8(sourceFile.readAll());
    const QRegularExpression regex(QStringLiteral("matchCi\\(start, end, \"([^\"]+)\"\\)"));
    QSet<QString> uniqueAliases;
    QStringList aliases;
    auto matchIterator = regex.globalMatch(content);
    while (matchIterator.hasNext()) {
        const auto match = matchIterator.next();
        const auto alias = match.captured(1).trimmed();
        if (alias.isEmpty() || uniqueAliases.contains(alias)) continue;
        uniqueAliases.insert(alias);
        aliases.push_back(alias);
    }
    return aliases;
}

QString buildAliasesSuffix(const QStringList& availableAliases,
                           const QStringList& selectedAliases) {
    QStringList orderedSelected;
    orderedSelected.reserve(selectedAliases.size());
    for (const auto& alias : availableAliases) {
        if (selectedAliases.contains(alias)) orderedSelected.push_back(alias);
    }
    if (orderedSelected.isEmpty()) return {};
    return QStringLiteral(".aliases(%1)").arg(orderedSelected.join(QStringLiteral(",")));
}

}  // namespace

QStringList loadAliasesForChannel(const char* channelName) {
    const auto discoveredAliases = extractAliasesFromCxetSource();
    const auto channel = QString::fromUtf8(channelName);
    if (channel == QStringLiteral("trades")) {
        return orderedAliases(canonicalTradesAliases(), discoveredAliases);
    }
    if (channel == QStringLiteral("bookticker")) {
        return orderedAliases(canonicalBookTickerAliases(), discoveredAliases);
    }
    if (channel == QStringLiteral("orderbook")) {
        return orderedAliases(canonicalOrderbookAliases(), discoveredAliases);
    }
    return {};
}

QStringList requiredAliasesForChannel(const QString& channel) {
    if (channel == QStringLiteral("trades")) {
        return {
            QStringLiteral("price"),
            QStringLiteral("amount"),
            QStringLiteral("side"),
            QStringLiteral("timestamp"),
        };
    }
    if (channel == QStringLiteral("bookticker")) {
        return {
            QStringLiteral("bidPrice"),
            QStringLiteral("askPrice"),
            QStringLiteral("timestamp"),
        };
    }
    if (channel == QStringLiteral("orderbook")) {
        return {
            QStringLiteral("bidPrice"),
            QStringLiteral("bidQty"),
            QStringLiteral("askPrice"),
            QStringLiteral("askQty"),
            QStringLiteral("timestamp"),
        };
    }
    return {};
}

bool isRequiredAliasForChannel(const QString& channel, const QString& alias) {
    return containsAlias(requiredAliasesForChannel(channel), alias);
}

QStringList normalizedSelectedAliasesForChannel(const QString& channel,
                                                const QStringList& selectedAliases) {
    QStringList normalized = selectedAliases;
    for (const auto& alias : requiredAliasesForChannel(channel)) {
        if (!normalized.contains(alias)) normalized.push_back(alias);
    }
    return normalized;
}

int aliasWeightBytes(const QString& alias) {
    if (alias == QStringLiteral("price") ||
        alias == QStringLiteral("amount") ||
        alias == QStringLiteral("quoteQty") ||
        alias == QStringLiteral("timestamp") ||
        alias == QStringLiteral("id") ||
        alias == QStringLiteral("firstTradeId") ||
        alias == QStringLiteral("lastTradeId") ||
        alias == QStringLiteral("updateId") ||
        alias == QStringLiteral("bidPrice") ||
        alias == QStringLiteral("bidQty") ||
        alias == QStringLiteral("askPrice") ||
        alias == QStringLiteral("askQty")) {
        return 8;
    }
    if (alias == QStringLiteral("side") ||
        alias == QStringLiteral("isBuyerMaker") ||
        alias == QStringLiteral("exchange") ||
        alias == QStringLiteral("market")) {
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
            if (alias == QStringLiteral("timestamp") ||
                alias == QStringLiteral("updateId") ||
                alias == QStringLiteral("symbol") ||
                alias == QStringLiteral("exchange") ||
                alias == QStringLiteral("market")) {
                baseBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("bidPrice") || alias == QStringLiteral("bidQty")) {
                perBidBytes += aliasWeightBytes(alias);
            } else if (alias == QStringLiteral("askPrice") || alias == QStringLiteral("askQty")) {
                perAskBytes += aliasWeightBytes(alias);
            }
        }
        return QStringLiteral("Total %1B + N_bid*%2B + N_ask*%3B")
            .arg(baseBytes)
            .arg(perBidBytes)
            .arg(perAskBytes);
    }

    int totalBytes = 0;
    for (const auto& alias : normalizedAliases) {
        totalBytes += aliasWeightBytes(alias);
    }
    return QStringLiteral("Total %1B").arg(totalBytes);
}

std::vector<std::string> normalizedSymbols(const QString& symbolsText) {
    std::vector<std::string> symbols;
    const auto rawTokens = symbolsText.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    symbols.reserve(rawTokens.size());
    for (const auto& rawToken : rawTokens) {
        const auto normalized = normalizeToken(rawToken);
        if (normalized.isEmpty()) continue;
        const auto asStd = normalized.toStdString();
        if (std::find(symbols.begin(), symbols.end(), asStd) == symbols.end()) {
            symbols.push_back(asStd);
        }
    }
    return symbols;
}

QString buildRequestPreview(const QString& channel,
                            const QStringList& availableAliases,
                            const QStringList& selectedAliases,
                            const QString& symbolsText) {
    const auto normalizedSelectedAliases = normalizedSelectedAliasesForChannel(channel, selectedAliases);
    const auto aliasesSuffix = buildAliasesSuffix(availableAliases, normalizedSelectedAliases);

    const auto symbols = normalizedSymbols(symbolsText);
    if (symbols.empty()) {
        return QStringLiteral("subscribe().object(%1).exchange(binance).market(futures).symbol(<symbol>)%2")
            .arg(channel, aliasesSuffix);
    }

    QStringList commands;
    commands.reserve(static_cast<qsizetype>(symbols.size()));
    for (const auto& symbol : symbols) {
        commands.push_back(
            QStringLiteral("subscribe().object(%1).exchange(binance).market(futures).symbol(%2)%3")
                .arg(channel, toDslSymbol(symbol), aliasesSuffix));
    }
    return commands.join(QStringLiteral("\n"));
}

std::vector<capture::CaptureConfig> makeConfigs(const QString& outputDirectory,
                                                const QString& symbolsText,
                                                const QStringList& tradesAvailableAliases,
                                                const QStringList& bookTickerAvailableAliases,
                                                const QStringList& orderbookAvailableAliases,
                                                const QStringList& selectedTradesAliases,
                                                const QStringList& selectedBookTickerAliases,
                                                const QStringList& selectedOrderbookAliases) {
    std::vector<capture::CaptureConfig> configs;
    const auto symbols = normalizedSymbols(symbolsText);
    configs.reserve(symbols.size());

    const auto normalizedTradesAliases =
        normalizedSelectedAliasesForChannel(QStringLiteral("trades"), selectedTradesAliases);
    const auto normalizedBookTickerAliases =
        normalizedSelectedAliasesForChannel(QStringLiteral("bookticker"), selectedBookTickerAliases);
    const auto normalizedOrderbookAliases =
        normalizedSelectedAliasesForChannel(QStringLiteral("orderbook"), selectedOrderbookAliases);
    const auto tradesSuffix = buildAliasesSuffix(tradesAvailableAliases, normalizedTradesAliases);
    const auto bookTickerSuffix = buildAliasesSuffix(bookTickerAvailableAliases, normalizedBookTickerAliases);
    const auto orderbookSuffix = buildAliasesSuffix(orderbookAvailableAliases, normalizedOrderbookAliases);

    for (const auto& symbol : symbols) {
        capture::CaptureConfig config{};
        config.exchange = "binance";
        config.market = "futures_usd";
        config.symbols = {symbol};
        config.outputDir = std::filesystem::path{outputDirectory.toStdString()};
        config.durationSec = 1800;
        config.snapshotIntervalSec = 60;

        const auto symbolDsl = toDslSymbol(symbol);
        config.tradesAliases.reserve(static_cast<std::size_t>(normalizedTradesAliases.size()));
        for (const auto& alias : normalizedTradesAliases) {
            config.tradesAliases.push_back(alias.toStdString());
        }
        config.bookTickerAliases.reserve(static_cast<std::size_t>(normalizedBookTickerAliases.size()));
        for (const auto& alias : normalizedBookTickerAliases) {
            config.bookTickerAliases.push_back(alias.toStdString());
        }
        config.orderbookAliases.reserve(static_cast<std::size_t>(normalizedOrderbookAliases.size()));
        for (const auto& alias : normalizedOrderbookAliases) {
            config.orderbookAliases.push_back(alias.toStdString());
        }
        config.tradesRequestCommand =
            QStringLiteral("subscribe().object(trades).exchange(binance).market(futures).symbol(%1)%2")
                .arg(symbolDsl, tradesSuffix)
                .toStdString();
        config.bookTickerRequestCommand =
            QStringLiteral("subscribe().object(bookticker).exchange(binance).market(futures).symbol(%1)%2")
                .arg(symbolDsl, bookTickerSuffix)
                .toStdString();
        config.orderbookRequestCommand =
            QStringLiteral("subscribe().object(orderbook).exchange(binance).market(futures).symbol(%1)%2")
                .arg(symbolDsl, orderbookSuffix)
                .toStdString();
        configs.push_back(std::move(config));
    }

    return configs;
}

}  // namespace hftrec::gui::detail
