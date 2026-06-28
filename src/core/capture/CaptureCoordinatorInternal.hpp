#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"
#if HFTREC_WITH_CXET
#include "cxet.hpp"
#endif

namespace hftrec::corpus {
struct InstrumentMetadata;
}  // namespace hftrec::corpus

namespace hftrec::capture::internal {

void ensureCxetInitialized() noexcept;
Status loadCaptureEnv(const CaptureConfig& config, std::string& lastError) noexcept;
std::int64_t nowNs() noexcept;
long long nowSec() noexcept;
std::uint8_t normalizedApiSlot(const CaptureConfig& config) noexcept;
std::string_view primaryIdentitySymbolText(const CaptureConfig& config) noexcept;
std::string_view primaryRouteSymbolText(const CaptureConfig& config) noexcept;

#if HFTREC_WITH_CXET
cxet::UnifiedRequestBuilder makeTradesBuilder(const CaptureConfig& config) noexcept;
cxet::UnifiedRequestBuilder makeBookTickerBuilder(const CaptureConfig& config) noexcept;
cxet::UnifiedRequestBuilder makeLiquidationBuilder(const CaptureConfig& config) noexcept;
cxet::UnifiedRequestBuilder makeOrderbookSubscribeBuilder(const CaptureConfig& config) noexcept;

bool applyRequestedAliases(const std::vector<std::string>& aliasNames,
                           cxet::UnifiedRequestBuilder& builder,
                           std::string& lastError);

#endif

void enrichInstrumentMetadataFromExchangeInfo(const CaptureConfig& config,
                                              corpus::InstrumentMetadata& metadata) noexcept;
Status validateSupportedConfig(const CaptureConfig& config, std::string& lastError);
bool sessionConfigMatches(const CaptureConfig& lhs, const CaptureConfig& rhs) noexcept;

}  // namespace hftrec::capture::internal
