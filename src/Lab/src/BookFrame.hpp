#pragma once

#include <cstdint>
#include <vector>

namespace hftrec::lab {

struct BookLevelView {
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
};

struct BookFrame {
    std::int64_t tsNs{0};

    std::int64_t bestBidPriceE8{0};
    std::int64_t bestBidQtyE8{0};
    std::int64_t bestAskPriceE8{0};
    std::int64_t bestAskQtyE8{0};

    bool hasBookTicker{false};
    std::int64_t tickerBidPriceE8{0};
    std::int64_t tickerBidQtyE8{0};
    std::int64_t tickerAskPriceE8{0};
    std::int64_t tickerAskQtyE8{0};

    std::vector<BookLevelView> topBids{};
    std::vector<BookLevelView> topAsks{};
    std::size_t totalBidLevels{0};
    std::size_t totalAskLevels{0};
};

}  // namespace hftrec::lab
