#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/tui/RecorderTuiSymbols.hpp"

namespace {

using hftrec::tui::SymbolBatchInput;
using hftrec::tui::allCryptoVenueSpecs;
using hftrec::tui::generateJobsForSymbols;
using hftrec::tui::loadSymbolBatchInput;
using hftrec::tui::renderSymbolListText;
using hftrec::tui::symbolListConfigDir;
using hftrec::tui::venueSymbolsFromGlobalInput;

TEST(RecorderTuiSymbols, KeepsLocalGlobalSymbolsForCryptoVenues) {
    EXPECT_EQ(venueSymbolsFromGlobalInput("kucoin_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("gate_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("xt_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("bingx_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("toobit_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("htx_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("phemex_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("okx_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("okx_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("toobit_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("xt_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("bingx_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("htx_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("phemex_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("mexc_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("bitmart_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("bitmart_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("poloniex_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("poloniex_spot", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("hyperliquid_futures", "BTC_USDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("hyperliquid_futures", "1000_PEPE_USDT"), "1000_PEPE_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("hyperliquid_futures", "BTCUSDC"), "");
}

TEST(RecorderTuiSymbols, RejectsBareAndNumericTokens) {
    SymbolBatchInput out{};
    std::string error;

    EXPECT_FALSE(loadSymbolBatchInput("1,4USDT", std::filesystem::path{}, out, error));
    EXPECT_NE(error.find("BASE_QUOTE"), std::string::npos);

    EXPECT_FALSE(loadSymbolBatchInput("BTC", std::filesystem::path{}, out, error));
    EXPECT_NE(error.find("BASE_QUOTE"), std::string::npos);
    EXPECT_TRUE(out.loadedFiles.empty());

    EXPECT_FALSE(loadSymbolBatchInput("1_PEPE_USDT", std::filesystem::path{}, out, error));
    EXPECT_NE(error.find("BASE_QUOTE"), std::string::npos);
}

TEST(RecorderTuiSymbols, LoadsIniTokenFromSymbolListDirectory) {
    const auto root = std::filesystem::temp_directory_path() / "hftrec_tui_symbols_test";
    std::filesystem::create_directories(root);
    const auto listPath = root / "1.ini";
    {
        std::ofstream file(listPath);
        file << "# hot list\n";
        file << "LAB_USDT,RE_USDT\n";
        file << "1000_PEPE_USDT\n";
    }

    SymbolBatchInput out{};
    std::string error;
    ASSERT_TRUE(loadSymbolBatchInput("1.ini,ALLO_USDT", root, out, error)) << error;

    ASSERT_EQ(out.symbols.size(), 4u);
    EXPECT_EQ(out.symbols[0], "LAB_USDT");
    EXPECT_EQ(out.symbols[1], "RE_USDT");
    EXPECT_EQ(out.symbols[2], "1000_PEPE_USDT");
    EXPECT_EQ(out.symbols[3], "ALLO_USDT");
    ASSERT_EQ(out.loadedFiles.size(), 1u);
    EXPECT_EQ(out.loadedFiles.front(), listPath);
}

TEST(RecorderTuiSymbols, RejectsListPrefixWithoutIniExtension) {
    SymbolBatchInput out{};
    std::string error;

    EXPECT_FALSE(loadSymbolBatchInput("l:1", symbolListConfigDir(), out, error));
    EXPECT_NE(error.find(".ini"), std::string::npos);
}

TEST(RecorderTuiSymbols, RendersSymbolListWithoutGeneratedJobs) {
    EXPECT_EQ(renderSymbolListText({"ALLO_USDT", "LAB_USDT", "1000_PEPE_USDT"}),
              "ALLO_USDT\nLAB_USDT\n1000_PEPE_USDT\n");
}

TEST(RecorderTuiSymbols, GeneratesRequiredMarketDataJobsForAllCryptoVenues) {
    const auto jobs = generateJobsForSymbols({"LAB_USDT"}, allCryptoVenueSpecs(), 0);

    ASSERT_EQ(allCryptoVenueSpecs().size(), 31u);
    ASSERT_EQ(jobs.size(), allCryptoVenueSpecs().size());
    EXPECT_EQ(jobs.front().exchange, "binance");
    EXPECT_EQ(jobs.front().market, "futures");
    EXPECT_EQ(jobs.front().symbol, "LAB_USDT");
    EXPECT_TRUE(jobs.front().routeSymbol.empty());
    for (const auto& job : jobs) {
        EXPECT_TRUE(job.channels.trades);
        EXPECT_TRUE(job.channels.bookTicker);
        EXPECT_TRUE(job.channels.orderbook);
        const bool derivatives = job.market != "spot";
        EXPECT_EQ(job.channels.liquidations, derivatives);
        EXPECT_EQ(job.channels.markPrice, derivatives);
        EXPECT_EQ(job.channels.indexPrice, derivatives);
        EXPECT_EQ(job.channels.funding, derivatives);
        EXPECT_EQ(job.channels.priceLimit,
                  derivatives && (job.exchange == "bybit" || job.exchange == "okx"));
    }

    bool foundKucoinFutures = false;
    bool foundOkxFutures = false;
    bool foundMexcFutures = false;
    bool foundBitmartFutures = false;
    bool foundPoloniexFutures = false;
    bool foundPoloniexSpot = false;
    bool foundHyperliquidFutures = false;
    for (const auto& job : jobs) {
        if (job.exchange == "kucoin" && job.market == "futures" &&
            job.symbol == "LAB_USDT" && job.routeSymbol.empty()) {
            foundKucoinFutures = true;
        }
        if (job.exchange == "okx" && job.market == "futures" &&
            job.symbol == "LAB_USDT" && job.routeSymbol.empty()) {
            foundOkxFutures = true;
        }
        if (job.exchange == "mexc" && job.market == "futures" &&
            job.symbol == "LAB_USDT" && job.routeSymbol.empty()) {
            foundMexcFutures = true;
        }
        if (job.exchange == "bitmart" && job.market == "futures" &&
            job.symbol == "LAB_USDT" && job.routeSymbol.empty()) {
            foundBitmartFutures = true;
        }
        if (job.exchange == "poloniex" && job.market == "futures" &&
            job.symbol == "LAB_USDT" && job.routeSymbol.empty()) {
            foundPoloniexFutures = true;
        }
        if (job.exchange == "poloniex" && job.market == "spot" &&
            job.symbol == "LAB_USDT" && job.routeSymbol.empty()) {
            foundPoloniexSpot = true;
        }
        if (job.exchange == "hyperliquid" && job.market == "futures" &&
            job.symbol == "LAB_USDT" && job.routeSymbol.empty()) {
            foundHyperliquidFutures = true;
        }
    }

    EXPECT_TRUE(foundKucoinFutures);
    EXPECT_TRUE(foundOkxFutures);
    EXPECT_TRUE(foundMexcFutures);
    EXPECT_TRUE(foundBitmartFutures);
    EXPECT_TRUE(foundPoloniexFutures);
    EXPECT_TRUE(foundPoloniexSpot);
    EXPECT_TRUE(foundHyperliquidFutures);
}

TEST(RecorderTuiSymbols, DoesNotGenerateBareBaseSymbolJobs) {
    const auto jobs = generateJobsForSymbols({"BTC"}, allCryptoVenueSpecs(), 0);

    EXPECT_TRUE(jobs.empty());
}

}  // namespace
