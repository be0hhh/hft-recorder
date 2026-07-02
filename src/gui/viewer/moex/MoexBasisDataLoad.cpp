#include "gui/viewer/moex/MoexBasisDataLoad.hpp"

#include <QDir>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/capture/SessionManifest.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/replay/SessionReplay.hpp"

namespace hftrec::gui::viewer::moex {
namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

QString cleanPath(const std::filesystem::path& path) {
    return QDir::cleanPath(QString::fromStdString(path.string()));
}

bool readSessionManifest(const std::filesystem::path& sessionPath,
                         hftrec::capture::SessionManifest& manifest) {
    const std::string text = readFile(sessionPath / "manifest.json");
    return !text.empty() && isOk(hftrec::capture::parseManifestJson(text, manifest));
}

std::filesystem::path existingSessionFile(const std::filesystem::path& sessionPath,
                                          std::string_view manifestPath,
                                          std::string_view fallback) {
    if (!manifestPath.empty()) {
        std::filesystem::path path{std::string{manifestPath}};
        if (path.is_relative()) path = sessionPath / path;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) return path;
    }
    const auto fallbackPath = sessionPath / std::string{fallback};
    std::error_code ec;
    return std::filesystem::exists(fallbackPath, ec) && !ec ? fallbackPath : std::filesystem::path{};
}

std::filesystem::path candlePathFor(const std::filesystem::path& sessionPath,
                                    const hftrec::capture::SessionManifest& manifest,
                                    bool& detailed) {
    detailed = true;
    auto path = existingSessionFile(sessionPath, manifest.candles2Path, "jsonl/candles2.jsonl");
    if (!path.empty()) return path;
    path = existingSessionFile(sessionPath, {}, "jsonl/candlesv2.jsonl");
    if (!path.empty()) return path;
    detailed = false;
    return existingSessionFile(sessionPath, manifest.candlesPath, "jsonl/candles.jsonl");
}

bool loadMetadata(const std::filesystem::path& sessionPath,
                  std::int64_t& priceBasisQtyE8,
                  std::int64_t& expiryUtcNs) {
    hftrec::corpus::InstrumentMetadata metadata{};
    const std::string text = readFile(sessionPath / "instrument_metadata.json");
    if (text.empty() || !isOk(hftrec::corpus::parseInstrumentMetadataJson(text, metadata))) return false;
    if (metadata.priceBasisQtyE8.has_value()) priceBasisQtyE8 = *metadata.priceBasisQtyE8;
    if (metadata.expiryUtcNs.has_value()) expiryUtcNs = *metadata.expiryUtcNs;
    return true;
}

QString symbolKey(QString value) {
    return value.trimmed().toUpper();
}

hftrec::replay::CandleRow candleFromSeriesRow(const hftrec::recordings::BasisChainSeriesRow& row) {
    hftrec::replay::CandleRow out{};
    out.tsNs = row.tsNs;
    out.exchange = row.exchange;
    out.market = row.market;
    out.symbol = row.symbol;
    out.timeframe = row.timeframe;
    out.durationNs = row.durationNs;
    out.openE8 = row.openE8 > 0 ? row.openE8 : row.closeE8;
    out.highE8 = row.highE8 > 0 ? row.highE8 : std::max(out.openE8, row.closeE8);
    out.lowE8 = row.lowE8 > 0 ? row.lowE8 : std::min(out.openE8, row.closeE8);
    out.closeE8 = row.closeE8;
    out.volumeE8 = row.volumeE8;
    out.quoteAmountE8 = row.quoteAmountE8;
    out.hasOhlc = out.tsNs > 0 && out.openE8 > 0 && out.highE8 > 0 &&
                  out.lowE8 > 0 && out.closeE8 > 0 && out.highE8 >= out.lowE8;
    return out;
}

}  // namespace

MoexBasisController::LegState loadLeg(const hftrec::recordings::RecordedSessionInfo& session,
                                      const QString& role,
                                      LegLoadMode mode) {
    MoexBasisController::LegState leg{};
    leg.role = role;
    leg.label = QStringLiteral("%1/%2 %3")
                    .arg(QString::fromStdString(session.exchange),
                         QString::fromStdString(session.market),
                         QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front()));
    leg.symbol = QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front());
    leg.exchange = QString::fromStdString(session.exchange);
    leg.market = QString::fromStdString(session.market);
    leg.sessionPath = cleanPath(session.path);
    leg.priceBasisQtyE8 = 100000000LL;
    leg.metadataReady = role == QStringLiteral("spot");

    const std::filesystem::path path = session.path;
    (void)loadMetadata(path, leg.priceBasisQtyE8, leg.expiryUtcNs);
    if (role == QStringLiteral("future")) {
        leg.metadataReady = leg.priceBasisQtyE8 > 0 && leg.expiryUtcNs > 0;
    }

    hftrec::capture::SessionManifest manifest{};
    if (!readSessionManifest(path, manifest)) {
        leg.status = QStringLiteral("missing manifest");
        return leg;
    }
    if (mode == LegLoadMode::MetadataOnly) {
        leg.status = leg.metadataReady ? QStringLiteral("series candles") : QStringLiteral("missing expiry/price basis");
        return leg;
    }

    bool detailed = true;
    const auto candlePath = candlePathFor(path, manifest, detailed);
    if (candlePath.empty()) {
        leg.status = QStringLiteral("missing candles");
        return leg;
    }

    hftrec::replay::SessionReplay replay{};
    const auto status = detailed ? replay.addCandles2File(candlePath) : replay.addCandlesFile(candlePath);
    if (!isOk(status)) {
        leg.status = QStringLiteral("failed to load candles");
        return leg;
    }
    const auto& rows = detailed ? replay.candles2() : replay.candles();
    leg.candles = selectMoexBasisCandles(rows);
    if (leg.candles.empty()) leg.status = QStringLiteral("no valid candles");
    else if (!leg.metadataReady) leg.status = QStringLiteral("missing expiry/price basis");
    else leg.status = QStringLiteral("%1 candles").arg(static_cast<qulonglong>(leg.candles.size()));
    return leg;
}

bool applyBasisChainSeriesRows(const std::vector<hftrec::recordings::BasisChainSeriesRow>& rows,
                               MoexBasisController::LegState& spot,
                               std::vector<MoexBasisController::LegState>& futures) {
    std::vector<hftrec::replay::CandleRow> spotCandles;
    std::map<QString, std::vector<hftrec::replay::CandleRow>> futuresBySymbol;
    std::map<QString, std::int64_t> expiryBySymbol;
    for (const auto& row : rows) {
        hftrec::replay::CandleRow candle = candleFromSeriesRow(row);
        if (!candle.hasOhlc || moexBasisClosePriceE8(candle) <= 0) continue;
        const QString key = symbolKey(QString::fromStdString(row.symbol));
        if (row.kind == "spot") {
            spotCandles.push_back(std::move(candle));
        } else if (row.kind == "future" && !key.isEmpty()) {
            futuresBySymbol[key].push_back(std::move(candle));
            if (row.expiryUtcNs > 0) expiryBySymbol[key] = row.expiryUtcNs;
        }
    }
    auto sortCandles = [](auto& candles) {
        std::stable_sort(candles.begin(), candles.end(), [](const auto& lhs, const auto& rhs) noexcept {
            return lhs.tsNs < rhs.tsNs;
        });
    };
    sortCandles(spotCandles);
    spot.candles = std::move(spotCandles);
    if (!spot.candles.empty()) {
        spot.status = QStringLiteral("%1 series candles").arg(static_cast<qulonglong>(spot.candles.size()));
        spot.metadataReady = true;
    } else {
        spot.status = QStringLiteral("series has no spot candles");
    }

    int futureWithCandles = 0;
    for (auto& future : futures) {
        const QString key = symbolKey(future.symbol);
        auto candlesIt = futuresBySymbol.find(key);
        if (candlesIt == futuresBySymbol.end() || candlesIt->second.empty()) {
            future.candles.clear();
            future.status = QStringLiteral("series has no candles");
            continue;
        }
        sortCandles(candlesIt->second);
        future.candles = std::move(candlesIt->second);
        const auto expiryIt = expiryBySymbol.find(key);
        if (future.expiryUtcNs <= 0 && expiryIt != expiryBySymbol.end()) future.expiryUtcNs = expiryIt->second;
        future.priceBasisQtyE8 = 100000000LL;
        future.metadataReady = future.expiryUtcNs > 0;
        future.status = future.metadataReady
            ? QStringLiteral("%1 series candles").arg(static_cast<qulonglong>(future.candles.size()))
            : QStringLiteral("missing expiry");
        if (future.metadataReady && !future.candles.empty()) ++futureWithCandles;
    }
    return !spot.candles.empty() && futureWithCandles > 0;
}

}  // namespace hftrec::gui::viewer::moex
