#include "core/arbitrage/BookTickerSpread.hpp"

#include <algorithm>

namespace hftrec::arbitrage {

namespace {

constexpr std::int64_t kBookTickerSpreadStaleGapNs = 1'000'000'000ll;

bool validQuote(const hftrec::replay::BookTickerRow& row) noexcept {
    return row.bidPriceE8 > 0 && row.askPriceE8 > 0 && row.askPriceE8 >= row.bidPriceE8;
}

bool freshQuote(std::int64_t tsNs, const hftrec::replay::BookTickerRow* row) noexcept {
    return row != nullptr && tsNs >= row->tsNs && tsNs - row->tsNs <= kBookTickerSpreadStaleGapNs;
}

BookTickerSpreadPoint makePoint(std::int64_t tsNs,
                                const hftrec::replay::BookTickerRow& a,
                                const hftrec::replay::BookTickerRow& b,
                                double feePenaltyBps) noexcept {
    const double closePenaltyE8 = static_cast<double>(a.askPriceE8 - a.bidPriceE8)
        + static_cast<double>(b.askPriceE8 - b.bidPriceE8);
    const double buyAAskSellBBidRaw = (static_cast<double>(b.bidPriceE8 - a.askPriceE8)
        / static_cast<double>(a.askPriceE8)) * 10000.0;
    const double buyBAskSellABidRaw = (static_cast<double>(a.bidPriceE8 - b.askPriceE8)
        / static_cast<double>(b.askPriceE8)) * 10000.0;
    const double buyAAskSellBBidPenalty = (closePenaltyE8 / static_cast<double>(a.askPriceE8)) * 10000.0;
    const double buyBAskSellABidPenalty = (closePenaltyE8 / static_cast<double>(b.askPriceE8)) * 10000.0;
    const double buyAAskSellBBid = buyAAskSellBBidRaw - buyAAskSellBBidPenalty;
    const double buyBAskSellABid = buyBAskSellABidRaw - buyBAskSellABidPenalty;

    BookTickerSpreadPoint out{};
    out.tsNs = tsNs;
    if (buyAAskSellBBid >= buyBAskSellABid) {
        out.rawSpreadBps = buyAAskSellBBidRaw;
        out.internalPenaltyBps = buyAAskSellBBidPenalty;
        out.feePenaltyBps = feePenaltyBps;
        out.spreadBps = buyAAskSellBBid;
        out.direction = SpreadDirection::BuyAAskSellBBid;
        out.buyAskPriceE8 = a.askPriceE8;
        out.sellBidPriceE8 = b.bidPriceE8;
    } else {
        out.rawSpreadBps = buyBAskSellABidRaw;
        out.internalPenaltyBps = buyBAskSellABidPenalty;
        out.feePenaltyBps = feePenaltyBps;
        out.spreadBps = buyBAskSellABid;
        out.direction = SpreadDirection::BuyBAskSellABid;
        out.buyAskPriceE8 = b.askPriceE8;
        out.sellBidPriceE8 = a.bidPriceE8;
    }
    return out;
}

}  // namespace

std::vector<BookTickerSpreadPoint> buildBestSideBookTickerSpread(
    const std::vector<hftrec::replay::BookTickerRow>& a,
    const std::vector<hftrec::replay::BookTickerRow>& b,
    double feePenaltyBps) {
    std::vector<BookTickerSpreadPoint> out;
    out.reserve(a.size() + b.size());

    std::size_t ai = 0u;
    std::size_t bi = 0u;
    const hftrec::replay::BookTickerRow* lastA = nullptr;
    const hftrec::replay::BookTickerRow* lastB = nullptr;

    while (ai < a.size() || bi < b.size()) {
        const bool takeA = bi >= b.size() || (ai < a.size() && a[ai].tsNs <= b[bi].tsNs);
        std::int64_t tsNs = 0;
        if (takeA) {
            tsNs = a[ai].tsNs;
            if (validQuote(a[ai])) lastA = &a[ai];
            ++ai;
        } else {
            tsNs = b[bi].tsNs;
            if (validQuote(b[bi])) lastB = &b[bi];
            ++bi;
        }

        if (!freshQuote(tsNs, lastA) || !freshQuote(tsNs, lastB)) continue;
        out.push_back(makePoint(tsNs, *lastA, *lastB, feePenaltyBps));
    }

    return out;
}

}  // namespace hftrec::arbitrage

