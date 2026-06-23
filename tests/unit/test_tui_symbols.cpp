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

TEST(RecorderTuiSymbols, FormatsGlobalSymbolForNativeCryptoVenues) {
    EXPECT_EQ(venueSymbolsFromGlobalInput("kucoin_futures", "BTCUSDT"), "XBTUSDTM");
    EXPECT_EQ(venueSymbolsFromGlobalInput("gate_futures", "BTCUSDT"), "BTC_USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("okx_futures", "BTCUSDT"), "BTC-USDT-SWAP");
    EXPECT_EQ(venueSymbolsFromGlobalInput("okx_spot", "BTCUSDT"), "BTC-USDT");
    EXPECT_EQ(venueSymbolsFromGlobalInput("mexc_futures", "BTCUSDT"), "BTC_USDT");
}

TEST(RecorderTuiSymbols, TreatsNumericBareTokensAsSymbols) {
    SymbolBatchInput out{};
    std::string error;

    ASSERT_TRUE(loadSymbolBatchInput("1,4USDT", std::filesystem::path{}, out, error)) << error;

    ASSERT_EQ(out.symbols.size(), 2u);
    EXPECT_EQ(out.symbols[0], "1");
    EXPECT_EQ(out.symbols[1], "4USDT");
    EXPECT_TRUE(out.loadedFiles.empty());
}

TEST(RecorderTuiSymbols, LoadsIniTokenFromSymbolListDirectory) {
    const auto root = std::filesystem::temp_directory_path() / "hftrec_tui_symbols_test";
    std::filesystem::create_directories(root);
    const auto listPath = root / "1.ini";
    {
        std::ofstream file(listPath);
        file << "# hot list\n";
        file << "lab,re\n";
        file << "h\n";
    }

    SymbolBatchInput out{};
    std::string error;
    ASSERT_TRUE(loadSymbolBatchInput("1.ini,allo", root, out, error)) << error;

    ASSERT_EQ(out.symbols.size(), 4u);
    EXPECT_EQ(out.symbols[0], "lab");
    EXPECT_EQ(out.symbols[1], "re");
    EXPECT_EQ(out.symbols[2], "h");
    EXPECT_EQ(out.symbols[3], "allo");
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
    EXPECT_EQ(renderSymbolListText({"allo", "lab", "4USDT"}), "allo\nlab\n4USDT\n");
}

TEST(RecorderTuiSymbols, GeneratesLiveJobsForAllCryptoVenues) {
    const auto jobs = generateJobsForSymbols({"lab"}, allCryptoVenueSpecs(), 0);

    ASSERT_EQ(jobs.size(), allCryptoVenueSpecs().size());
    EXPECT_EQ(jobs.front().exchange, "binance");
    EXPECT_EQ(jobs.front().market, "futures");
    EXPECT_EQ(jobs.front().symbol, "LABUSDT");
    EXPECT_TRUE(jobs.front().channels.trades);
    EXPECT_TRUE(jobs.front().channels.priceLimit);

    bool foundKucoinFutures = false;
    bool foundOkxFutures = false;
    bool foundMexcFutures = false;
    for (const auto& job : jobs) {
        if (job.exchange == "kucoin" && job.market == "futures" && job.symbol == "LABUSDTM") {
            foundKucoinFutures = true;
        }
        if (job.exchange == "okx" && job.market == "futures" && job.symbol == "LAB-USDT-SWAP") {
            foundOkxFutures = true;
        }
        if (job.exchange == "mexc" && job.market == "futures" && job.symbol == "LAB_USDT") {
            foundMexcFutures = true;
        }
    }

    EXPECT_TRUE(foundKucoinFutures);
    EXPECT_TRUE(foundOkxFutures);
    EXPECT_TRUE(foundMexcFutures);
}

}  // namespace
