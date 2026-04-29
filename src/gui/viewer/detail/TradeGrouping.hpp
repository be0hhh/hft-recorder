#pragma once

#include <algorithm>
#include <vector>

#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/detail/Formatters.hpp"

namespace hftrec::gui::viewer::detail {

inline TradeGroupEntry makeTradeGroupEntry(std::int64_t priceE8,
                                           std::int64_t qtyE8,
                                           bool sideBuy,
                                           int origIndex) {
    return TradeGroupEntry{priceE8, qtyE8, multiplyScaledE8(qtyE8, priceE8), sideBuy, origIndex};
}

inline void appendGroupedTradeDot(std::vector<TradeDot>& out, TradeDot dot) {
    const TradeGroupEntry entry = makeTradeGroupEntry(dot.priceE8, dot.qtyE8, dot.sideBuy, dot.origIndex);
    dot.firstOrigIndex = dot.origIndex;
    dot.lastOrigIndex = dot.origIndex;
    dot.totalQtyE8 = dot.qtyE8;
    dot.totalAmountE8 = entry.amountE8;
    dot.groupEntries = {entry};

    if (out.empty() || out.back().tsNs != dot.tsNs) {
        out.push_back(std::move(dot));
        return;
    }

    auto& group = out.back();
    group.totalQtyE8 += entry.qtyE8;
    group.totalAmountE8 += entry.amountE8;
    group.firstOrigIndex = group.firstOrigIndex < 0 ? entry.origIndex : std::min(group.firstOrigIndex, entry.origIndex);
    group.lastOrigIndex = group.lastOrigIndex < 0 ? entry.origIndex : std::max(group.lastOrigIndex, entry.origIndex);

    const auto currentAmountE8 = multiplyScaledE8(group.qtyE8, group.priceE8);
    if (entry.amountE8 > currentAmountE8) {
        group.priceE8 = entry.priceE8;
        group.qtyE8 = entry.qtyE8;
        group.sideBuy = entry.sideBuy;
        group.origIndex = entry.origIndex;
    }

    group.groupEntries.push_back(entry);
    std::sort(group.groupEntries.begin(), group.groupEntries.end(), [](const TradeGroupEntry& lhs, const TradeGroupEntry& rhs) {
        if (lhs.amountE8 != rhs.amountE8) return lhs.amountE8 > rhs.amountE8;
        return lhs.origIndex < rhs.origIndex;
    });
}

inline std::int64_t displayTradeQtyE8(const TradeDot& dot) noexcept {
    return dot.totalQtyE8 != 0 ? dot.totalQtyE8 : dot.qtyE8;
}

inline std::int64_t displayTradeAmountE8(const TradeDot& dot) noexcept {
    return dot.totalAmountE8 != 0 ? dot.totalAmountE8 : multiplyScaledE8(dot.qtyE8, dot.priceE8);
}

inline bool isGroupedTradeDot(const TradeDot& dot) noexcept {
    return dot.groupEntries.size() > 1u;
}

}  // namespace hftrec::gui::viewer::detail
