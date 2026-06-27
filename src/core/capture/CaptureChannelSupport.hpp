#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::capture {

enum class CaptureChannel : std::uint8_t {
    Trades,
    Liquidations,
    BookTicker,
    Orderbook,
    MarkPrice,
    IndexPrice,
    Funding,
    PriceLimit,
};

enum class CaptureChannelSkipReason : std::uint8_t {
    None,
    UnsupportedRoute,
    ApplyFailed,
    ConnectFailed,
    SubscribeSendFailed,
    ExchangeErrorFrame,
    ParseFailed,
    NoRows,
    InvalidConfig,
};

struct CaptureChannelDecision {
    CaptureChannel channel{CaptureChannel::Trades};
    bool requested{false};
    bool enabled{false};
    bool skipped{false};
    CaptureChannelSkipReason reason{CaptureChannelSkipReason::None};
    std::string detail{};
};

struct CaptureLaunchPlan {
    std::vector<CaptureChannelDecision> decisions{};

    [[nodiscard]] bool anyEnabled() const noexcept;
    [[nodiscard]] bool allRequestedEnabled() const noexcept;
    [[nodiscard]] bool channelEnabled(CaptureChannel channel) const noexcept;
    [[nodiscard]] std::vector<CaptureChannel> enabledChannels() const;
    [[nodiscard]] std::string skippedSummary() const;
};

using CaptureChannelAvailabilityFn = bool (*)(const CaptureConfig& config,
                                              CaptureChannel channel,
                                              std::string& detail,
                                              void* userData);

const char* captureChannelName(CaptureChannel channel) noexcept;
const char* captureChannelSkipReasonName(CaptureChannelSkipReason reason) noexcept;

bool captureChannelRuntimeReady(const CaptureConfig& config,
                                CaptureChannel channel,
                                std::string& detail) noexcept;

CaptureLaunchPlan buildCaptureLaunchPlan(const CaptureConfig& config,
                                          const std::vector<CaptureChannel>& requested,
                                          CaptureChannelAvailabilityFn availability = nullptr,
                                          void* userData = nullptr);

}  // namespace hftrec::capture

