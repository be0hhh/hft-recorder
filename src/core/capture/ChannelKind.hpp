#pragma once

#include <cstdint>
#include <string_view>

namespace hftrec::capture {

enum class ChannelKind : std::uint8_t {
    Trades     = 1,
    BookTicker = 2,
    DepthDelta = 3,
    Liquidations = 5,
    Candles = 6,
    DepthTape = 7,
    DepthSidecar = 8,
    MarkPrice = 9,
    IndexPrice = 10,
    Funding = 11,
    PriceLimit = 12,
    ExecutionFast = 13,
    FastFill = 14,
    ContractStats = 15,
    ContractInfo = 16,
    OkxBooks50L2Tbt = 17,
    BitgetRpiBooks = 18,
    Candles2 = 19,
};

constexpr std::string_view channelFileName(ChannelKind k) noexcept {
    switch (k) {
        case ChannelKind::Trades:     return "trades.jsonl";
        case ChannelKind::BookTicker: return "bookticker.jsonl";
        case ChannelKind::DepthDelta: return "depth.jsonl";
        case ChannelKind::Liquidations: return "liquidations.jsonl";
        case ChannelKind::Candles: return "candles.jsonl";
        case ChannelKind::DepthTape: return "depth_tape.jsonl";
        case ChannelKind::DepthSidecar: return "depth_sidecar.jsonl";
        case ChannelKind::MarkPrice: return "mark_price.jsonl";
        case ChannelKind::IndexPrice: return "index_price.jsonl";
        case ChannelKind::Funding: return "funding.jsonl";
        case ChannelKind::PriceLimit: return "price_limit.jsonl";
        case ChannelKind::Candles2: return "candles2.jsonl";
        case ChannelKind::ExecutionFast: return "execution_fast.jsonl";
        case ChannelKind::FastFill: return "fast_fill.jsonl";
        case ChannelKind::ContractStats: return "contract_stats.jsonl";
        case ChannelKind::ContractInfo: return "contract_info.jsonl";
        case ChannelKind::OkxBooks50L2Tbt: return "okx_books50_l2_tbt.jsonl";
        case ChannelKind::BitgetRpiBooks: return "bitget_rpi_books.jsonl";
    }
    return "unknown";
}


constexpr std::string_view channelJsonlRelativePath(ChannelKind k) noexcept {
    switch (k) {
        case ChannelKind::Trades:     return "jsonl/trades.jsonl";
        case ChannelKind::BookTicker: return "jsonl/bookticker.jsonl";
        case ChannelKind::DepthDelta: return "jsonl/depth.jsonl";
        case ChannelKind::Liquidations: return "jsonl/liquidations.jsonl";
        case ChannelKind::Candles: return "jsonl/candles.jsonl";
        case ChannelKind::DepthTape: return "jsonl/depth_tape.jsonl";
        case ChannelKind::DepthSidecar: return "jsonl/depth_sidecar.jsonl";
        case ChannelKind::MarkPrice: return "jsonl/mark_price.jsonl";
        case ChannelKind::IndexPrice: return "jsonl/index_price.jsonl";
        case ChannelKind::Funding: return "jsonl/funding.jsonl";
        case ChannelKind::PriceLimit: return "jsonl/price_limit.jsonl";
        case ChannelKind::Candles2: return "jsonl/candles2.jsonl";
        case ChannelKind::ExecutionFast: return "jsonl/execution_fast.jsonl";
        case ChannelKind::FastFill: return "jsonl/fast_fill.jsonl";
        case ChannelKind::ContractStats: return "jsonl/contract_stats.jsonl";
        case ChannelKind::ContractInfo: return "jsonl/contract_info.jsonl";
        case ChannelKind::OkxBooks50L2Tbt: return "jsonl/okx_books50_l2_tbt.jsonl";
        case ChannelKind::BitgetRpiBooks: return "jsonl/bitget_rpi_books.jsonl";
    }
    return "unknown";
}
constexpr std::string_view channelName(ChannelKind k) noexcept {
    switch (k) {
        case ChannelKind::Trades:     return "trades";
        case ChannelKind::BookTicker: return "bookticker";
        case ChannelKind::DepthDelta: return "depth";
        case ChannelKind::Liquidations: return "liquidations";
        case ChannelKind::Candles: return "candles";
        case ChannelKind::DepthTape: return "depth_tape";
        case ChannelKind::DepthSidecar: return "depth_sidecar";
        case ChannelKind::MarkPrice: return "mark_price";
        case ChannelKind::IndexPrice: return "index_price";
        case ChannelKind::Funding: return "funding";
        case ChannelKind::PriceLimit: return "price_limit";
        case ChannelKind::Candles2: return "candles2";
        case ChannelKind::ExecutionFast: return "execution_fast";
        case ChannelKind::FastFill: return "fast_fill";
        case ChannelKind::ContractStats: return "contract_stats";
        case ChannelKind::ContractInfo: return "contract_info";
        case ChannelKind::OkxBooks50L2Tbt: return "okx_books50_l2_tbt";
        case ChannelKind::BitgetRpiBooks: return "bitget_rpi_books";
    }
    return "unknown";
}

}  // namespace hftrec::capture
