#pragma once

#include <cstdint>
#include <limits>

namespace hftrec::arbitrage {

constexpr std::int64_t kPriceBasisScaleE8 = 100000000LL;

[[nodiscard]] constexpr std::int64_t effectivePriceBasisQtyE8(std::int64_t priceBasisQtyE8) noexcept {
    return priceBasisQtyE8 > 0 ? priceBasisQtyE8 : kPriceBasisScaleE8;
}

[[nodiscard]] inline std::int64_t normalizeNativePriceE8(std::int64_t nativePriceE8,
                                                         std::int64_t priceBasisQtyE8) noexcept {
    if (nativePriceE8 <= 0) return nativePriceE8;
    const std::int64_t basis = effectivePriceBasisQtyE8(priceBasisQtyE8);
    if (basis == kPriceBasisScaleE8) return nativePriceE8;

    const __int128 numerator =
        static_cast<__int128>(nativePriceE8) * static_cast<__int128>(kPriceBasisScaleE8);
    const __int128 normalized = numerator / static_cast<__int128>(basis);
    if (normalized > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(normalized);
}

}  // namespace hftrec::arbitrage
