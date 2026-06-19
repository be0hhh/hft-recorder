#include <gtest/gtest.h>

#include <QStringList>

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace {

using hftrec::gui::detail::makeConfigs;
using hftrec::gui::detail::makeDetailedCandlesConfigs;
using hftrec::gui::detail::venueSymbolPlaceholder;
using hftrec::gui::detail::venueSymbolsFromGlobalInput;

TEST(CaptureViewModelRequests, AppliesNativeVenueSymbolsFromGlobalInput) {
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("kucoin_futures"), QStringLiteral("BTCUSDT")),
              QStringLiteral("XBTUSDTM"));
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("gate_futures"), QStringLiteral("BTCUSDT")),
              QStringLiteral("BTC_USDT"));
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("okx_futures"), QStringLiteral("BTCUSDT")),
              QStringLiteral("BTC-USDT-SWAP"));
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("okx_spot"), QStringLiteral("BTCUSDT")),
              QStringLiteral("BTC-USDT"));
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("bitget_futures"), QStringLiteral("BTCUSDT")),
              QStringLiteral("BTCUSDT"));
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("moex_futures"), QStringLiteral("SiM6")),
              QStringLiteral("SiM6"));
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("moex_spot"), QStringLiteral("SBER")),
              QStringLiteral("SBER"));
}

TEST(CaptureViewModelRequests, VenuePlaceholdersShowNativeFormat) {
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("kucoin_futures")), QStringLiteral("Example: XBTUSDTM"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("gate_futures")), QStringLiteral("Example: BTC_USDT"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("okx_futures")), QStringLiteral("Example: BTC-USDT-SWAP"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("moex_futures")), QStringLiteral("Example: SiM6"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("moex_spot")), QStringLiteral("Example: SBER"));
}

TEST(CaptureViewModelRequests, EmptyPerVenueSymbolDoesNotFallBackToGlobalSymbol) {
    const auto configs = makeConfigs(QStringLiteral("/tmp/hftrec-empty-symbol"),
                                     QStringLiteral("/tmp/.env"),
                                     1,
                                     QStringList{QStringLiteral("okx_futures")},
                                     QStringList{QStringLiteral("BTCUSDT"), QStringLiteral(""), QStringLiteral(""), QStringLiteral(""),
                                                 QStringLiteral(""), QStringLiteral(""), QStringLiteral(""), QStringLiteral(""),
                                                 QStringLiteral(""), QStringLiteral(""), QStringLiteral(""), QStringLiteral(""),
                                                 QStringLiteral(""), QStringLiteral("")},
                                     QStringLiteral("BTCUSDT"),
                                     QStringList{},
                                     QStringList{},
                                     QStringList{},
                                     QStringList{},
                                     QStringList{},
                                     QStringList{},
                                     QStringList{},
                                     QStringList{},
                                     300);
    EXPECT_TRUE(configs.empty());
}

TEST(CaptureViewModelRequests, DetailedCandlesUsesSelectedMoexVenueSymbols) {
    QString error;
    const auto configs = makeDetailedCandlesConfigs(QStringLiteral("/tmp/hftrec-moex-candles"),
                                                    QStringLiteral("/tmp/.env"),
                                                    1,
                                                    QStringList{QStringLiteral("moex_futures")},
                                                    QStringList{QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("XBTUSDTM"), QStringLiteral("BTC-USDT"),
                                                                QStringLiteral("BTC_USDT"), QStringLiteral("BTC_USDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTC-USDT-SWAP"), QStringLiteral("BTC-USDT"),
                                                                QStringLiteral("SiM6"), QStringLiteral("SBER")},
                                                    QStringLiteral("10m"),
                                                    5000,
                                                    &error);
    ASSERT_EQ(configs.size(), 1u);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(configs.front().exchange, "moex");
    EXPECT_EQ(configs.front().market, "futures");
    ASSERT_EQ(configs.front().symbols.size(), 1u);
    EXPECT_EQ(configs.front().symbols.front(), "SiM6");
    EXPECT_EQ(configs.front().detailedCandlesTimeframe, "10m");
    EXPECT_EQ(configs.front().detailedCandlesUnderlyingSymbolHint, "SBER");
}

TEST(CaptureViewModelRequests, DetailedCandlesMapsMoexFifteenMinuteToTenMinuteInMixedSelection) {
    QString error;
    const auto configs = makeDetailedCandlesConfigs(QStringLiteral("/tmp/hftrec-moex-candles"),
                                                    QStringLiteral("/tmp/.env"),
                                                    1,
                                                    QStringList{QStringLiteral("binance_futures"), QStringLiteral("moex_futures")},
                                                    QStringList{QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("XBTUSDTM"), QStringLiteral("BTC-USDT"),
                                                                QStringLiteral("BTC_USDT"), QStringLiteral("BTC_USDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTC-USDT-SWAP"), QStringLiteral("BTC-USDT"),
                                                                QStringLiteral("SiM6"), QStringLiteral("SBER")},
                                                    QStringLiteral("15m"),
                                                    5000,
                                                    &error);
    ASSERT_EQ(configs.size(), 2u);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(configs[0].exchange, "binance");
    EXPECT_EQ(configs[0].detailedCandlesTimeframe, "15m");
    EXPECT_EQ(configs[1].exchange, "moex");
    EXPECT_EQ(configs[1].detailedCandlesTimeframe, "10m");
    EXPECT_EQ(configs[1].detailedCandlesUnderlyingSymbolHint, "SBER");
}

TEST(CaptureViewModelRequests, DetailedCandlesRejectsUnsupportedMoexTimeframe) {
    QString error;
    const auto configs = makeDetailedCandlesConfigs(QStringLiteral("/tmp/hftrec-moex-candles"),
                                                    QStringLiteral("/tmp/.env"),
                                                    1,
                                                    QStringList{QStringLiteral("moex_futures")},
                                                    QStringList{QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("XBTUSDTM"), QStringLiteral("BTC-USDT"),
                                                                QStringLiteral("BTC_USDT"), QStringLiteral("BTC_USDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTCUSDT"), QStringLiteral("BTCUSDT"),
                                                                QStringLiteral("BTC-USDT-SWAP"), QStringLiteral("BTC-USDT"),
                                                                QStringLiteral("SiM6"), QStringLiteral("SBER")},
                                                    QStringLiteral("5m"),
                                                    5000,
                                                    &error);
    EXPECT_TRUE(configs.empty());
    EXPECT_FALSE(error.isEmpty());
}

}  // namespace
