#include <gtest/gtest.h>

#include <QStringList>
#include <QVariantMap>

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace {

using hftrec::gui::detail::makeConfigs;
using hftrec::gui::detail::makeDetailedCandlesConfigs;
using hftrec::gui::detail::detailedCandlesVenueChoices;
using hftrec::gui::detail::detailedCandlesTimeframeChoices;
using hftrec::gui::detail::venueChoices;
using hftrec::gui::detail::venueSymbolPlaceholder;
using hftrec::gui::detail::venueSymbolsFromGlobalInput;

QStringList choiceValues(const QVariantList& choices) {
    QStringList out;
    for (const auto& rawChoice : choices) out.push_back(rawChoice.toMap().value(QStringLiteral("value")).toString());
    return out;
}

QStringList choiceKeys(const QVariantList& choices) {
    QStringList out;
    for (const auto& rawChoice : choices) out.push_back(rawChoice.toMap().value(QStringLiteral("key")).toString());
    return out;
}

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
    EXPECT_EQ(venueSymbolsFromGlobalInput(QStringLiteral("finam_spot"), QStringLiteral("SBER@MISX")),
              QStringLiteral("SBER@MISX"));
}

TEST(CaptureViewModelRequests, VenuePlaceholdersShowNativeFormat) {
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("kucoin_futures")), QStringLiteral("Example: XBTUSDTM"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("gate_futures")), QStringLiteral("Example: BTC_USDT"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("okx_futures")), QStringLiteral("Example: BTC-USDT-SWAP"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("finam_spot")), QStringLiteral("Example: SBER@MISX"));
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

TEST(CaptureViewModelRequests, DetailedCandlesBuildsSingleFinamConfigFromForm) {
    QString error;
    const auto configs = makeDetailedCandlesConfigs(QStringLiteral("/tmp/hftrec-finam-candles"),
                                                    QStringLiteral("/tmp/.env"),
                                                    1,
                                                    QStringLiteral("finam_spot"),
                                                    QStringLiteral("SBER@MISX"),
                                                    QStringLiteral("1m"),
                                                    5000,
                                                    &error);
    ASSERT_EQ(configs.size(), 1u);
    EXPECT_TRUE(error.isEmpty());
    EXPECT_EQ(configs.front().exchange, "finam");
    EXPECT_EQ(configs.front().market, "spot");
    ASSERT_EQ(configs.front().symbols.size(), 1u);
    EXPECT_EQ(configs.front().symbols.front(), "SBER@MISX");
    EXPECT_EQ(configs.front().detailedCandlesTimeframe, "1m");
}

TEST(CaptureViewModelRequests, DetailedCandlesRejectsMultipleSymbols) {
    QString error;
    const auto configs = makeDetailedCandlesConfigs(QStringLiteral("/tmp/hftrec-candles"),
                                                    QStringLiteral("/tmp/.env"),
                                                    1,
                                                    QStringLiteral("binance_futures"),
                                                    QStringLiteral("BTCUSDT ETHUSDT"),
                                                    QStringLiteral("1m"),
                                                    5000,
                                                    &error);
    EXPECT_TRUE(configs.empty());
    EXPECT_EQ(error, QStringLiteral("Enter one detailed candles symbol"));
}

TEST(CaptureViewModelRequests, DetailedCandlesTimeframeChoicesAreFilteredByVenue) {
    const auto finam = choiceValues(detailedCandlesTimeframeChoices(QStringLiteral("finam_spot")));
    EXPECT_TRUE(finam.contains(QStringLiteral("1M")));
    EXPECT_FALSE(finam.contains(QStringLiteral("10m")));

    const auto okx = choiceValues(detailedCandlesTimeframeChoices(QStringLiteral("okx_futures")));
    EXPECT_TRUE(okx.contains(QStringLiteral("1M")));
    EXPECT_FALSE(okx.contains(QStringLiteral("8h")));
    EXPECT_FALSE(okx.contains(QStringLiteral("3d")));

    const auto kucoin = choiceValues(detailedCandlesTimeframeChoices(QStringLiteral("kucoin_spot")));
    EXPECT_TRUE(kucoin.contains(QStringLiteral("4h")));

    const auto bitget = choiceValues(detailedCandlesTimeframeChoices(QStringLiteral("bitget_futures")));
    EXPECT_TRUE(bitget.contains(QStringLiteral("12h")));
    EXPECT_FALSE(bitget.contains(QStringLiteral("1w")));
}

TEST(CaptureViewModelRequests, DetailedCandlesVenueChoicesExcludeUnsupportedKlinesVenues) {
    const auto liveKeys = choiceKeys(venueChoices());
    const auto candleKeys = choiceKeys(detailedCandlesVenueChoices());
    EXPECT_TRUE(liveKeys.contains(QStringLiteral("mexc_spot")));
    EXPECT_FALSE(liveKeys.contains(QStringLiteral("moex_futures")));
    EXPECT_FALSE(liveKeys.contains(QStringLiteral("moex_spot")));
    EXPECT_FALSE(candleKeys.contains(QStringLiteral("mexc_spot")));
    EXPECT_FALSE(candleKeys.contains(QStringLiteral("moex_futures")));
    EXPECT_TRUE(candleKeys.contains(QStringLiteral("finam_spot")));
}

TEST(CaptureViewModelRequests, DetailedCandlesRejectsUnsupportedFinamTimeframe) {
    QString error;
    const auto configs = makeDetailedCandlesConfigs(QStringLiteral("/tmp/hftrec-finam-candles"),
                                                    QStringLiteral("/tmp/.env"),
                                                    1,
                                                    QStringLiteral("finam_spot"),
                                                    QStringLiteral("SBER@MISX"),
                                                    QStringLiteral("10m"),
                                                    5000,
                                                    &error);
    EXPECT_TRUE(configs.empty());
    EXPECT_FALSE(error.isEmpty());
}

}  // namespace
