#include <gtest/gtest.h>

#include "gui/viewer/VenueColors.hpp"

namespace {

TEST(VenueColors, NormalizesConfiguredExchanges) {
    EXPECT_EQ(hftrec::gui::viewer::venueColor(QStringLiteral("KuCoin")), QColor(22, 132, 73));
    EXPECT_EQ(hftrec::gui::viewer::venueColor(QStringLiteral("gate")), QColor(48, 96, 175));
    EXPECT_EQ(hftrec::gui::viewer::venueColor(QStringLiteral("gate-io")), QColor(48, 96, 175));
    EXPECT_EQ(hftrec::gui::viewer::venueColor(QStringLiteral("bit_get")), QColor(24, 154, 170));
    EXPECT_EQ(hftrec::gui::viewer::venueColor(QStringLiteral("BINANCE")), QColor(196, 154, 36));
}

TEST(VenueColors, UnknownExchangeUsesMutedFallback) {
    EXPECT_EQ(hftrec::gui::viewer::venueColor(QStringLiteral("unknown")), QColor(128, 128, 136));
}

}  // namespace
