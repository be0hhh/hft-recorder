#include <gtest/gtest.h>

#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "core/recordings/BasisChainManifest.hpp"
#include "core/recordings/BasisChainSeries.hpp"
#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace {

using hftrec::gui::detail::detailedCandlesBasisChainCandidates;
using hftrec::gui::detail::enabledBasisChainSymbols;
using hftrec::gui::detail::makeDetailedCandlesBasisChainConfigs;

hftrec::replay::CandleRow candle(std::int64_t tsNs,
                                 std::string symbol,
                                 std::int64_t closeE8,
                                 std::int64_t priceBumpE8 = 0) {
    hftrec::replay::CandleRow row;
    row.tier = 1;
    row.tsNs = tsNs;
    row.exchange = "finam";
    row.market = "test";
    row.symbol = std::move(symbol);
    row.timeframe = "1m";
    row.durationNs = 60000000000ll;
    row.openE8 = closeE8 - priceBumpE8;
    row.highE8 = closeE8 + priceBumpE8;
    row.lowE8 = closeE8 - priceBumpE8;
    row.closeE8 = closeE8;
    row.hasOhlc = true;
    return row;
}

TEST(BasisChainManifest, RendersSpotAndFutureRoles) {
    hftrec::recordings::BasisChainManifest manifest;
    manifest.groupId = "2026-06-29_SBRF_basis_chain";
    manifest.title = "SBRF basis chain";
    manifest.underlying = "SBRF";
    manifest.timeframe = "1m";
    manifest.requestedEndNs = 1781901900000000000ll;
    manifest.requestedLimit = 10080;
    manifest.createdAtNs = 1781901900000000001ll;
    manifest.legs.push_back({"spot", "finam_spot_SBER", "finam_spot_SBER", "finam", "spot", "SBER@MISX", "SBRF", "", "ok", "", 10080});
    manifest.legs.push_back({"future", "finam_futures_SRU6", "finam_futures_SRU6", "finam", "futures", "SRU6@RTSX", "SBRF", "2026-09-17", "ok", "", 10080});

    const std::string json = hftrec::recordings::renderBasisChainManifestJson(manifest);

    EXPECT_NE(json.find("\"schema\": \"hftrec.basis_chain.v1\""), std::string::npos);
    EXPECT_NE(json.find("\"series_path\": \"basis_chain_series.jsonl\""), std::string::npos);
    EXPECT_NE(json.find("\"role\": \"spot\""), std::string::npos);
    EXPECT_NE(json.find("\"role\": \"future\""), std::string::npos);
    EXPECT_NE(json.find("\"symbol\": \"SRU6@RTSX\""), std::string::npos);
}

TEST(BasisChainSeries, RanksOverlappingFuturesByExpiry) {
    hftrec::recordings::BasisChainSeriesLegInput spot;
    spot.role = "spot";
    spot.exchange = "finam";
    spot.market = "spot";
    spot.symbol = "SBER@MISX";
    spot.candles = {candle(1000, "SBER@MISX", 10000000000ll)};

    hftrec::recordings::BasisChainSeriesLegInput nearFuture;
    nearFuture.role = "future";
    nearFuture.exchange = "finam";
    nearFuture.market = "futures";
    nearFuture.symbol = "SRM6@RTSX";
    nearFuture.expiryUtcNs = 5000;
    nearFuture.priceBasisQtyE8 = 100000000ll;
    nearFuture.candles = {candle(1000, "SRM6@RTSX", 10100000000ll)};

    hftrec::recordings::BasisChainSeriesLegInput farFuture = nearFuture;
    farFuture.symbol = "SRU6@RTSX";
    farFuture.expiryUtcNs = 7000;
    farFuture.candles = {candle(1000, "SRU6@RTSX", 10200000000ll)};

    hftrec::recordings::BasisChainSeriesStats stats;
    const auto rows = hftrec::recordings::buildBasisChainSeriesRows({spot, farFuture, nearFuture}, &stats);

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(stats.rows, 3u);
    EXPECT_EQ(stats.futuresCount, 2);
    EXPECT_EQ(stats.frontRankCount, 2);
    const auto nearIt = std::find_if(rows.begin(), rows.end(), [](const auto& row) { return row.symbol == "SRM6@RTSX"; });
    const auto farIt = std::find_if(rows.begin(), rows.end(), [](const auto& row) { return row.symbol == "SRU6@RTSX"; });
    ASSERT_NE(nearIt, rows.end());
    ASSERT_NE(farIt, rows.end());
    EXPECT_EQ(nearIt->contractOrder, 1);
    EXPECT_EQ(nearIt->frontRank, 1);
    EXPECT_EQ(farIt->contractOrder, 2);
    EXPECT_EQ(farIt->frontRank, 2);
    EXPECT_GT(nearIt->basisBpsE8, 0);
}

TEST(BasisChainSeries, RendersLongJsonLine) {
    hftrec::recordings::BasisChainSeriesRow row;
    row.kind = "future";
    row.tsNs = 1000;
    row.symbol = "SRM6@RTSX";
    row.exchange = "finam";
    row.market = "futures";
    row.timeframe = "1m";
    row.closeE8 = 10100000000ll;
    row.expiryUtcNs = 5000;
    row.contractOrder = 1;
    row.frontRank = 1;
    row.basisBpsE8 = 10000000000ll;

    const std::string json = hftrec::recordings::renderBasisChainSeriesJsonLine(row);

    EXPECT_NE(json.find("\"kind\": \"future\""), std::string::npos);
    EXPECT_NE(json.find("\"symbol\": \"SRM6@RTSX\""), std::string::npos);
    EXPECT_NE(json.find("\"front_rank\": 1"), std::string::npos);
    EXPECT_NE(json.find("\"basis_bps_e8\": 10000000000"), std::string::npos);
}

TEST(BasisChainSeries, ParsesRenderedJsonLine) {
    hftrec::recordings::BasisChainSeriesRow row;
    row.kind = "future";
    row.tsNs = 1000;
    row.symbol = "SRM6@RTSX";
    row.exchange = "finam";
    row.market = "futures";
    row.timeframe = "1m";
    row.durationNs = 60000000000ll;
    row.openE8 = 10000000000ll;
    row.highE8 = 10200000000ll;
    row.lowE8 = 9900000000ll;
    row.closeE8 = 10100000000ll;
    row.volumeE8 = 500000000ll;
    row.quoteAmountE8 = 50500000000ll;
    row.expiryUtcNs = 5000;
    row.priceBasisQtyE8 = 100000000ll;
    row.contractOrder = 1;
    row.frontRank = 1;
    row.basisBpsE8 = 10000000000ll;

    hftrec::recordings::BasisChainSeriesRow parsed;
    ASSERT_TRUE(hftrec::recordings::parseBasisChainSeriesJsonLine(
        hftrec::recordings::renderBasisChainSeriesJsonLine(row), parsed));

    EXPECT_EQ(parsed.kind, row.kind);
    EXPECT_EQ(parsed.tsNs, row.tsNs);
    EXPECT_EQ(parsed.symbol, row.symbol);
    EXPECT_EQ(parsed.exchange, row.exchange);
    EXPECT_EQ(parsed.market, row.market);
    EXPECT_EQ(parsed.timeframe, row.timeframe);
    EXPECT_EQ(parsed.durationNs, row.durationNs);
    EXPECT_EQ(parsed.openE8, row.openE8);
    EXPECT_EQ(parsed.highE8, row.highE8);
    EXPECT_EQ(parsed.lowE8, row.lowE8);
    EXPECT_EQ(parsed.closeE8, row.closeE8);
    EXPECT_EQ(parsed.volumeE8, row.volumeE8);
    EXPECT_EQ(parsed.quoteAmountE8, row.quoteAmountE8);
    EXPECT_EQ(parsed.expiryUtcNs, row.expiryUtcNs);
    EXPECT_EQ(parsed.priceBasisQtyE8, row.priceBasisQtyE8);
    EXPECT_EQ(parsed.contractOrder, row.contractOrder);
    EXPECT_EQ(parsed.frontRank, row.frontRank);
    EXPECT_EQ(parsed.basisBpsE8, row.basisBpsE8);
}

TEST(BasisChainSeries, ReadsGroupSeriesFile) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "hftrec_basis_chain_series_reader";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    hftrec::recordings::BasisChainSeriesRow spot;
    spot.kind = "spot";
    spot.tsNs = 1000;
    spot.symbol = "SBER@MISX";
    spot.exchange = "finam";
    spot.market = "spot";
    spot.timeframe = "1m";
    spot.closeE8 = 10000000000ll;

    hftrec::recordings::BasisChainSeriesRow future;
    future.kind = "future";
    future.tsNs = 1000;
    future.symbol = "SRM6@RTSX";
    future.exchange = "finam";
    future.market = "futures";
    future.timeframe = "1m";
    future.closeE8 = 10100000000ll;
    future.expiryUtcNs = 5000;
    future.priceBasisQtyE8 = 100000000ll;
    future.contractOrder = 1;
    future.frontRank = 1;
    future.basisBpsE8 = 10000000000ll;

    {
        std::ofstream out(dir / "basis_chain_series.jsonl", std::ios::binary | std::ios::trunc);
        out << hftrec::recordings::renderBasisChainSeriesJsonLine(spot) << '\n';
        out << '\n';
        out << hftrec::recordings::renderBasisChainSeriesJsonLine(future) << '\n';
    }

    std::vector<hftrec::recordings::BasisChainSeriesRow> rows;
    std::string error;
    ASSERT_TRUE(hftrec::recordings::readBasisChainSeries(dir, rows, &error)) << error;

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].kind, "spot");
    EXPECT_EQ(rows[1].kind, "future");
    EXPECT_EQ(rows[1].basisBpsE8, 10000000000ll);

    std::filesystem::remove_all(dir);
}

TEST(BasisChainHelpers, SelectsEditableFinamFuturesChainForSpot) {
    QString error;
    const QVariantList candidates = detailedCandlesBasisChainCandidates(QStringLiteral("finam_spot"),
                                                                        QStringLiteral("SBER@MISX"),
                                                                        3,
                                                                        20,
                                                                        &error);
    ASSERT_FALSE(candidates.isEmpty());
    EXPECT_TRUE(error.isEmpty());
    const QVariantMap first = candidates.front().toMap();
    EXPECT_TRUE(first.value(QStringLiteral("symbol")).toString().startsWith(QStringLiteral("SR")));
    EXPECT_EQ(first.value(QStringLiteral("underlying")).toString(), QStringLiteral("SBRF"));
    EXPECT_TRUE(first.value(QStringLiteral("enabled")).toBool());

    const QStringList enabled = enabledBasisChainSymbols(candidates);
    EXPECT_LE(enabled.size(), 3);
    EXPECT_FALSE(enabled.isEmpty());
}

TEST(BasisChainHelpers, BuildsSpotFirstConfigsForSelectedChain) {
    QVariantList candidates;
    QVariantMap disabledFuture;
    disabledFuture.insert(QStringLiteral("symbol"), QStringLiteral("SRM6@RTSX"));
    disabledFuture.insert(QStringLiteral("enabled"), false);
    candidates.push_back(disabledFuture);
    QVariantMap enabledFuture;
    enabledFuture.insert(QStringLiteral("symbol"), QStringLiteral("SRU6@RTSX"));
    enabledFuture.insert(QStringLiteral("enabled"), true);
    candidates.push_back(enabledFuture);

    QString error;
    const auto configs = makeDetailedCandlesBasisChainConfigs(QStringLiteral("/tmp/hftrec-basis-chain"),
                                                              QStringLiteral("/tmp/.env"),
                                                              1,
                                                              QStringLiteral("finam_spot"),
                                                              QStringLiteral("SBER@MISX"),
                                                              QStringLiteral("finam_futures"),
                                                              candidates,
                                                              QStringLiteral("1m"),
                                                              10080,
                                                              &error,
                                                              1781901900000000000ll);
    ASSERT_EQ(configs.size(), 2u);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(configs[0].market, "spot");
    EXPECT_EQ(configs[0].symbols.front(), "SBER@MISX");
    EXPECT_EQ(configs[1].market, "futures");
    EXPECT_EQ(configs[1].symbols.front(), "SRU6@RTSX");
    EXPECT_EQ(configs[1].outputDir, std::filesystem::path{"/tmp/hftrec-basis-chain"});
    EXPECT_EQ(configs[1].detailedCandlesEndNs, 1781901900000000000ll);
}

}  // namespace
