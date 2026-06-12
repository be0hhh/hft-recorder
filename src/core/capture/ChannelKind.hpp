#pragma once

#include <cstdint>
#include <string_view>

namespace hftrec::capture {

enum class ChannelKind : std::uint8_t {
    Trades     = 1,
    BookTicker = 2,
    DepthDelta = 3,
    Snapshot   = 4,
    Liquidations = 5,
    Candles = 6,
    DepthTape = 7,
    DepthSidecar = 8,
};

constexpr std::string_view channelFileName(ChannelKind k) noexcept {
    switch (k) {
        case ChannelKind::Trades:     return "trades.jsonl";
        case ChannelKind::BookTicker: return "bookticker.jsonl";
        case ChannelKind::DepthDelta: return "depth.jsonl";
        case ChannelKind::Snapshot:   return "snapshot_000.json";
        case ChannelKind::Liquidations: return "liquidations.jsonl";
        case ChannelKind::Candles: return "candles.jsonl";
        case ChannelKind::DepthTape: return "depth_tape.jsonl";
        case ChannelKind::DepthSidecar: return "depth_sidecar.jsonl";
    }
    return "unknown";
}


constexpr std::string_view channelJsonlRelativePath(ChannelKind k) noexcept {
    switch (k) {
        case ChannelKind::Trades:     return "jsonl/trades.jsonl";
        case ChannelKind::BookTicker: return "jsonl/bookticker.jsonl";
        case ChannelKind::DepthDelta: return "jsonl/depth.jsonl";
        case ChannelKind::Snapshot:   return "snapshot_000.json";
        case ChannelKind::Liquidations: return "jsonl/liquidations.jsonl";
        case ChannelKind::Candles: return "jsonl/candles.jsonl";
        case ChannelKind::DepthTape: return "jsonl/depth_tape.jsonl";
        case ChannelKind::DepthSidecar: return "jsonl/depth_sidecar.jsonl";
    }
    return "unknown";
}
constexpr std::string_view channelName(ChannelKind k) noexcept {
    switch (k) {
        case ChannelKind::Trades:     return "trades";
        case ChannelKind::BookTicker: return "bookticker";
        case ChannelKind::DepthDelta: return "depth";
        case ChannelKind::Snapshot:   return "snapshot";
        case ChannelKind::Liquidations: return "liquidations";
        case ChannelKind::Candles: return "candles";
        case ChannelKind::DepthTape: return "depth_tape";
        case ChannelKind::DepthSidecar: return "depth_sidecar";
    }
    return "unknown";
}

}  // namespace hftrec::capture
