#include "gui/viewmodels/CaptureViewModelInternal.hpp"
#include "gui/viewmodels/FinamCatalog.hpp"

#include <QVariantMap>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

namespace hftrec::gui::detail {
namespace {

constexpr int kBasisChainMaxCandidateRows = 80;
constexpr std::uint32_t kFinamDetailedCandlesAttempts = 10u;

bool venueFromKey(const QString& venueKey, std::string& exchange, std::string& market) {
    const QString key = venueKey.trimmed().toLower();
    if (key == QStringLiteral("finam_spot")) {
        exchange = "finam";
        market = "spot";
        return true;
    }
    if (key == QStringLiteral("finam_futures")) {
        exchange = "finam";
        market = "futures";
        return true;
    }
    const QStringList parts = key.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    if (parts.size() < 2) return false;
    exchange = parts.front().toStdString();
    market = parts.back() == QStringLiteral("spot") ? "spot" : "futures";
    return !exchange.empty();
}

capture::CaptureConfig makeChainConfig(const QString& outputDirectory,
                                       const QString& envPath,
                                       int apiSlot,
                                       const QString& venueKey,
                                       const std::string& symbol,
                                       const QString& timeframe,
                                       int limit,
                                       std::int64_t endNs) {
    capture::CaptureConfig config{};
    (void)venueFromKey(venueKey, config.exchange, config.market);
    config.symbols = {symbol};
    config.envPath = std::filesystem::path{envPath.toStdString()};
    config.apiSlot = static_cast<std::uint8_t>(std::clamp(apiSlot, 1, 255));
    config.outputDir = std::filesystem::path{outputDirectory.toStdString()};
    config.detailedCandlesTimeframe = timeframe.trimmed().toStdString();
    config.detailedCandlesLimit = static_cast<std::uint32_t>(std::clamp(limit, 1, 1'000'000));
    config.detailedCandlesEndNs = endNs > 0 ? endNs : 0;
    if (config.exchange == "finam") {
        config.detailedCandlesMaxAttemptsPerPage = kFinamDetailedCandlesAttempts;
    }
    return config;
}

QString candidateRightText(const QVariantMap& row) {
    QStringList parts;
    const QString expiration = row.value(QStringLiteral("expiration")).toString();
    if (!expiration.isEmpty()) parts.push_back(expiration);
    parts.push_back(row.value(QStringLiteral("archived")).toBool() ? QStringLiteral("archived") : QStringLiteral("active"));
    return parts.join(QStringLiteral(" / "));
}

}  // namespace

QVariantList detailedCandlesModeChoices() {
    QVariantList out;
    auto append = [&](const QString& label, const QString& value) {
        QVariantMap row;
        row.insert(QStringLiteral("label"), label);
        row.insert(QStringLiteral("value"), value);
        out.push_back(row);
    };
    append(QStringLiteral("Single"), QStringLiteral("single"));
    append(QStringLiteral("Pair"), QStringLiteral("pair"));
    append(QStringLiteral("Basis Chain"), QStringLiteral("basis_chain"));
    return out;
}

QVariantList detailedCandlesBasisChainCandidates(const QString& spotVenueKey,
                                                 const QString& spotSymbolText,
                                                 int maxEnabled,
                                                 int maxRows,
                                                 QString* errorText) {
    if (errorText != nullptr) errorText->clear();
    QVariantList out;
    if (spotVenueKey.trimmed().toLower() != QStringLiteral("finam_spot")) {
        if (errorText != nullptr) *errorText = QStringLiteral("Basis chain uses FINAM Spot as leg 1");
        return out;
    }
    const auto spotSymbols = normalizedSymbols(spotSymbolText);
    if (spotSymbols.empty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter FINAM spot symbol first");
        return out;
    }
    if (spotSymbols.size() != 1u) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter one FINAM spot symbol for basis chain");
        return out;
    }

    const QString underlying = finamAnchorUnderlying(spotSymbolText);
    if (underlying.isEmpty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Cannot resolve FINAM underlying for spot symbol");
        return out;
    }

    const FinamCatalog& catalog = finamCatalog();
    auto it = catalog.futuresByUnderlying.constFind(underlying);
    if (it == catalog.futuresByUnderlying.constEnd() || it.value().empty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("No FINAM futures found for underlying %1").arg(underlying);
        return out;
    }

    std::vector<const FinamSymbolRow*> rows = it.value();
    std::sort(rows.begin(), rows.end(), [](const FinamSymbolRow* lhs, const FinamSymbolRow* rhs) {
        if (lhs == nullptr || rhs == nullptr) return rhs != nullptr;
        if (lhs->isArchived != rhs->isArchived) return !lhs->isArchived;
        if (lhs->expiration != rhs->expiration) return lhs->expiration > rhs->expiration;
        return lhs->symbol < rhs->symbol;
    });

    const int rowLimit = std::clamp(maxRows <= 0 ? kBasisChainMaxCandidateRows : maxRows, 1, kBasisChainMaxCandidateRows);
    const int enabledLimit = std::clamp(maxEnabled, 0, rowLimit);
    int enabledCount = 0;
    for (const FinamSymbolRow* rowPtr : rows) {
        if (rowPtr == nullptr) continue;
        const FinamSymbolRow& row = *rowPtr;
        QVariantMap item = finamSuggestionItem(row, finamSuggestionRank(row, QStringLiteral("futures"), {}, underlying));
        item.insert(QStringLiteral("index"), out.size());
        item.insert(QStringLiteral("enabled"), enabledCount < enabledLimit);
        item.insert(QStringLiteral("rightText"), candidateRightText(item));
        if (enabledCount < enabledLimit) ++enabledCount;
        out.push_back(item);
        if (out.size() >= rowLimit) break;
    }
    return out;
}

QStringList enabledBasisChainSymbols(const QVariantList& candidateRows) {
    QStringList out;
    for (const QVariant& value : candidateRows) {
        const QVariantMap row = value.toMap();
        if (!row.value(QStringLiteral("enabled")).toBool()) continue;
        const QString symbol = row.value(QStringLiteral("symbol")).toString().trimmed();
        if (!symbol.isEmpty() && !out.contains(symbol)) out.push_back(symbol);
    }
    return out;
}

std::vector<capture::CaptureConfig> makeDetailedCandlesBasisChainConfigs(const QString& outputDirectory,
                                                                         const QString& envPath,
                                                                         int apiSlot,
                                                                         const QString& spotVenueKey,
                                                                         const QString& spotSymbolText,
                                                                         const QString& futuresVenueKey,
                                                                         const QVariantList& candidateRows,
                                                                         const QString& timeframe,
                                                                         int limit,
                                                                         QString* errorText,
                                                                         std::int64_t endNs) {
    if (errorText != nullptr) errorText->clear();
    std::vector<capture::CaptureConfig> configs;
    if (spotVenueKey.trimmed().toLower() != QStringLiteral("finam_spot") ||
        futuresVenueKey.trimmed().toLower() != QStringLiteral("finam_futures")) {
        if (errorText != nullptr) *errorText = QStringLiteral("Basis chain requires FINAM Spot + FINAM Futures");
        return configs;
    }
    const auto spotSymbols = normalizedSymbols(spotSymbolText);
    if (spotSymbols.empty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter FINAM spot symbol");
        return configs;
    }
    if (spotSymbols.size() != 1u) {
        if (errorText != nullptr) *errorText = QStringLiteral("Enter one FINAM spot symbol");
        return configs;
    }

    const QStringList futures = enabledBasisChainSymbols(candidateRows);
    if (futures.isEmpty()) {
        if (errorText != nullptr) *errorText = QStringLiteral("Select at least one futures contract");
        return configs;
    }
    const QString tf = timeframe.trimmed().isEmpty() ? QStringLiteral("1m") : timeframe.trimmed();
    configs.reserve(static_cast<std::size_t>(1 + futures.size()));
    configs.push_back(makeChainConfig(outputDirectory, envPath, apiSlot, spotVenueKey, spotSymbols.front(), tf, limit, endNs));
    for (const QString& future : futures) {
        configs.push_back(makeChainConfig(outputDirectory, envPath, apiSlot, futuresVenueKey, future.toStdString(), tf, limit, endNs));
    }
    return configs;
}

QString buildDetailedCandlesBasisChainPreview(const QString& spotVenueKey,
                                              const QString& spotSymbolText,
                                              const QVariantList& candidateRows,
                                              const QString& timeframe,
                                              int limit) {
    const QStringList futures = enabledBasisChainSymbols(candidateRows);
    const QString tf = timeframe.trimmed().isEmpty() ? QStringLiteral("1m") : timeframe.trimmed();
    QStringList lines;
    lines.push_back(QStringLiteral("basis_chain().spot(%1/%2).timeframe(%3).limit(%4)")
        .arg(spotVenueKey, spotSymbolText.trimmed().isEmpty() ? QStringLiteral("<spot>") : spotSymbolText.trimmed(), tf, QString::number(limit)));
    lines.push_back(QStringLiteral("futures selected: %1").arg(QString::number(futures.size())));
    if (!futures.isEmpty()) {
        lines.push_back(QStringLiteral("contracts: %1").arg(futures.mid(0, 12).join(QStringLiteral(", "))));
    }
    return lines.join(QStringLiteral("\n"));
}

}  // namespace hftrec::gui::detail
