#include <gtest/gtest.h>

#include <QStringList>

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace {

using hftrec::gui::detail::makeConfigs;
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
}

TEST(CaptureViewModelRequests, VenuePlaceholdersShowNativeFormat) {
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("kucoin_futures")), QStringLiteral("Example: XBTUSDTM"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("gate_futures")), QStringLiteral("Example: BTC_USDT"));
    EXPECT_EQ(venueSymbolPlaceholder(QStringLiteral("okx_futures")), QStringLiteral("Example: BTC-USDT-SWAP"));
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

}  // namespace
