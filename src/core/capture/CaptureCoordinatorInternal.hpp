#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"
#include "cxet.hpp"

namespace hftrec::capture::internal {

inline constexpr std::string_view kSupportedExchange = "binance";
inline constexpr std::string_view kSupportedMarket = "futures_usd";

void ensureCxetInitialized() noexcept;
std::int64_t nowNs() noexcept;
long long nowSec() noexcept;

cxet::UnifiedRequestBuilder makeTradesBuilder(const std::string& symbolText) noexcept;
cxet::UnifiedRequestBuilder makeBookTickerBuilder(const std::string& symbolText) noexcept;
cxet::UnifiedRequestBuilder makeOrderbookSubscribeBuilder(const std::string& symbolText) noexcept;

bool applyRequestedAliases(const std::vector<std::string>& aliasNames,
                           cxet::UnifiedRequestBuilder& builder,
                           std::string& lastError);

Status validateSupportedConfig(const CaptureConfig& config, std::string& lastError);
bool sessionConfigMatches(const CaptureConfig& lhs, const CaptureConfig& rhs) noexcept;

}  // namespace hftrec::capture::internal
