#include <gtest/gtest.h>

#include <QJsonObject>
#include <QString>

#include "gui/api/LocalVenueOrderParser.hpp"

namespace {

QJsonObject orderPlace(const QJsonObject& params) {
    QJsonObject root;
    root.insert(QStringLiteral("id"), QStringLiteral("test-1"));
    root.insert(QStringLiteral("method"), QStringLiteral("order.place"));
    root.insert(QStringLiteral("params"), params);
    return root;
}

QJsonObject limitParams() {
    QJsonObject params;
    params.insert(QStringLiteral("symbol"), QStringLiteral("BTCUSDT"));
    params.insert(QStringLiteral("side"), QStringLiteral("SELL"));
    params.insert(QStringLiteral("type"), QStringLiteral("LIMIT"));
    params.insert(QStringLiteral("timeInForce"), QStringLiteral("GTC"));
    params.insert(QStringLiteral("quantity"), QStringLiteral("1"));
    params.insert(QStringLiteral("price"), QStringLiteral("100"));
    return params;
}

bool buildRequest(const QJsonObject& root,
                  cxet::network::local::hftrecorder::OrderRequestFrame& frame,
                  QString& error) noexcept {
    return hftrec::gui::api::detail::buildOrderRequest(root, frame, error);
}

}  // namespace

TEST(LocalVenueWsOrderParser, AcceptsOneWayReduceOnlyOrder) {
    auto params = limitParams();
    params.insert(QStringLiteral("positionSide"), QStringLiteral("BOTH"));
    params.insert(QStringLiteral("reduceOnly"), QStringLiteral("true"));

    cxet::network::local::hftrecorder::OrderRequestFrame frame{};
    QString error;
    ASSERT_TRUE(buildRequest(orderPlace(params), frame, error)) << error.toStdString();
    EXPECT_EQ(frame.reduceOnlySet, 1u);
    EXPECT_EQ(frame.reduceOnly, 1u);
}

TEST(LocalVenueWsOrderParser, AcceptsExplicitFalseAndNoopBinanceFlags) {
    auto params = limitParams();
    params.insert(QStringLiteral("positionSide"), QStringLiteral("BOTH"));
    params.insert(QStringLiteral("reduceOnly"), QStringLiteral("false"));
    params.insert(QStringLiteral("closePosition"), QStringLiteral("false"));
    params.insert(QStringLiteral("priceProtect"), QStringLiteral("false"));
    params.insert(QStringLiteral("newOrderRespType"), QStringLiteral("ACK"));
    params.insert(QStringLiteral("priceMatch"), QStringLiteral("NONE"));
    params.insert(QStringLiteral("selfTradePreventionMode"), QStringLiteral("NONE"));

    cxet::network::local::hftrecorder::OrderRequestFrame frame{};
    QString error;
    ASSERT_TRUE(buildRequest(orderPlace(params), frame, error)) << error.toStdString();
    EXPECT_EQ(frame.reduceOnlySet, 1u);
    EXPECT_EQ(frame.reduceOnly, 0u);
}

TEST(LocalVenueWsOrderParser, RejectsUnsupportedBinanceFlags) {
    const auto expectRejected = [](QJsonObject params, const QString& expectedError) {
        cxet::network::local::hftrecorder::OrderRequestFrame frame{};
        QString error;
        EXPECT_FALSE(buildRequest(orderPlace(params), frame, error));
        EXPECT_EQ(error, expectedError);
    };

    auto hedge = limitParams();
    hedge.insert(QStringLiteral("positionSide"), QStringLiteral("LONG"));
    expectRejected(hedge, QStringLiteral("unsupported_position_side"));

    auto closeAll = limitParams();
    closeAll.insert(QStringLiteral("closePosition"), QStringLiteral("true"));
    expectRejected(closeAll, QStringLiteral("unsupported_close_position"));

    auto ioc = limitParams();
    ioc.insert(QStringLiteral("timeInForce"), QStringLiteral("IOC"));
    expectRejected(ioc, QStringLiteral("unsupported_time_in_force"));

    auto priceMatch = limitParams();
    priceMatch.insert(QStringLiteral("priceMatch"), QStringLiteral("OPPONENT"));
    expectRejected(priceMatch, QStringLiteral("unsupported_price_match"));

    auto badClosePosition = limitParams();
    badClosePosition.insert(QStringLiteral("closePosition"), QStringLiteral("wat"));
    expectRejected(badClosePosition, QStringLiteral("bad_close_position"));

    auto priceProtect = limitParams();
    priceProtect.insert(QStringLiteral("priceProtect"), QStringLiteral("true"));
    expectRejected(priceProtect, QStringLiteral("unsupported_price_protect"));

    auto gtd = limitParams();
    gtd.insert(QStringLiteral("goodTillDate"), QStringLiteral("1710000000000"));
    expectRejected(gtd, QStringLiteral("unsupported_good_till_date"));

    auto trailing = limitParams();
    trailing.insert(QStringLiteral("callbackRate"), QStringLiteral("1.0"));
    expectRejected(trailing, QStringLiteral("unsupported_trigger_option"));
}