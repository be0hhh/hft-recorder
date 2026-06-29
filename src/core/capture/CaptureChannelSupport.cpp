#include "core/capture/CaptureChannelSupport.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

#if HFTREC_WITH_CXET
#include "api/market/MarketDataReactor.hpp"
#include "canon/MarketMapping.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "core/capture/CaptureCoordinatorInternal.hpp"
#endif

namespace hftrec::capture {

namespace {

#if HFTREC_WITH_CXET
bool textEqualsAscii(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        char a = lhs[i];
        char b = rhs[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

ExchangeId exchangeIdFromConfig(std::string_view exchange) noexcept {
    if (textEqualsAscii(exchange, "binance")) return canon::kExchangeIdBinance;
    if (textEqualsAscii(exchange, "bybit")) return canon::kExchangeIdBybit;
    if (textEqualsAscii(exchange, "kucoin")) return canon::kExchangeIdKucoin;
    if (textEqualsAscii(exchange, "gate")) return canon::kExchangeIdGate;
    if (textEqualsAscii(exchange, "bitget")) return canon::kExchangeIdBitget;
    if (textEqualsAscii(exchange, "aster")) return canon::kExchangeIdAster;
    if (textEqualsAscii(exchange, "hyperliquid")) return canon::kExchangeIdHyperliquid;
    if (textEqualsAscii(exchange, "okx")) return canon::kExchangeIdOkx;
    if (textEqualsAscii(exchange, "finam")) return canon::kExchangeIdFinam;
    if (textEqualsAscii(exchange, "finam_arena")) return canon::kExchangeIdFinamArena;
    if (textEqualsAscii(exchange, "mexc")) return canon::kExchangeIdMexc;
    if (textEqualsAscii(exchange, "xt")) return canon::kExchangeIdXt;
    if (textEqualsAscii(exchange, "bingx")) return canon::kExchangeIdBingx;
    if (textEqualsAscii(exchange, "bitmart")) return canon::kExchangeIdBitmart;
    if (textEqualsAscii(exchange, "toobit")) return canon::kExchangeIdToobit;
    if (textEqualsAscii(exchange, "htx")) return canon::kExchangeIdHtx;
    if (textEqualsAscii(exchange, "phemex")) return canon::kExchangeIdPhemex;
    return canon::kExchangeIdUnknown;
}

canon::MarketType marketTypeFromConfig(ExchangeId exchange, std::string_view market) noexcept {
    char apiMarket[64]{};
    if (market.size() + 1u < sizeof(apiMarket)) {
        for (std::size_t i = 0u; i < market.size(); ++i) {
            char c = market[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            apiMarket[i] = c;
        }
        const canon::MarketType mapped = canon::exchangeApiStringToCanonical(exchange, apiMarket);
        if (mapped.raw != canon::kMarketTypeUnknown.raw) return mapped;
    }
    if (textEqualsAscii(market, "spot") || textEqualsAscii(market, "shares")) return canon::kMarketTypeSpot;
    if (textEqualsAscii(market, "margin")) return canon::kMarketTypeMargin;
    if (textEqualsAscii(market, "inverse")) return canon::kMarketTypeInverse;
    if (textEqualsAscii(market, "swap")) return canon::kMarketTypeSwap;
    if (textEqualsAscii(market, "futures") ||
        textEqualsAscii(market, "forts") ||
        textEqualsAscii(market, "futures_usd")) return canon::kMarketTypeFutures;
    return canon::kMarketTypeUnknown;
}

cxet::api::market::PublicMarketDataStream streamForCaptureChannel(CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades: return cxet::api::market::PublicMarketDataStream::Trades;
        case CaptureChannel::Liquidations: return cxet::api::market::PublicMarketDataStream::Liquidations;
        case CaptureChannel::BookTicker: return cxet::api::market::PublicMarketDataStream::BookTicker;
        case CaptureChannel::Orderbook: return cxet::api::market::PublicMarketDataStream::Orderbook;
        case CaptureChannel::MarkPrice: return cxet::api::market::PublicMarketDataStream::MarkPrice;
        case CaptureChannel::IndexPrice: return cxet::api::market::PublicMarketDataStream::IndexPrice;
        case CaptureChannel::Funding: return cxet::api::market::PublicMarketDataStream::Funding;
        case CaptureChannel::PriceLimit: return cxet::api::market::PublicMarketDataStream::PriceLimit;
    }
    return cxet::api::market::PublicMarketDataStream::BookTicker;
}
#endif

bool defaultAvailability(const CaptureConfig& config,
                         CaptureChannel channel,
                         std::string& detail,
                         void*) {
    return captureChannelRuntimeReady(config, channel, detail);
}

}  // namespace

const char* captureChannelName(CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades: return "trades";
        case CaptureChannel::Liquidations: return "liquidations";
        case CaptureChannel::BookTicker: return "bookticker";
        case CaptureChannel::Orderbook: return "orderbook";
        case CaptureChannel::MarkPrice: return "mark_price";
        case CaptureChannel::IndexPrice: return "index_price";
        case CaptureChannel::Funding: return "funding";
        case CaptureChannel::PriceLimit: return "price_limit";
    }
    return "unknown";
}

const char* captureChannelSkipReasonName(CaptureChannelSkipReason reason) noexcept {
    switch (reason) {
        case CaptureChannelSkipReason::None: return "none";
        case CaptureChannelSkipReason::UnsupportedRoute: return "unsupported_route";
        case CaptureChannelSkipReason::ApplyFailed: return "apply_failed";
        case CaptureChannelSkipReason::ConnectFailed: return "connect_failed";
        case CaptureChannelSkipReason::SubscribeSendFailed: return "subscribe_send_failed";
        case CaptureChannelSkipReason::ExchangeErrorFrame: return "exchange_error_frame";
        case CaptureChannelSkipReason::ParseFailed: return "parse_failed";
        case CaptureChannelSkipReason::NoRows: return "no_rows";
        case CaptureChannelSkipReason::InvalidConfig: return "invalid_config";
    }
    return "unknown";
}

bool CaptureLaunchPlan::anyEnabled() const noexcept {
    return std::any_of(decisions.begin(), decisions.end(), [](const CaptureChannelDecision& decision) {
        return decision.enabled;
    });
}

bool CaptureLaunchPlan::allRequestedEnabled() const noexcept {
    for (const auto& decision : decisions) {
        if (decision.requested && !decision.enabled) return false;
    }
    return true;
}

bool CaptureLaunchPlan::channelEnabled(CaptureChannel channel) const noexcept {
    for (const auto& decision : decisions) {
        if (decision.channel == channel) return decision.enabled;
    }
    return false;
}

std::vector<CaptureChannel> CaptureLaunchPlan::enabledChannels() const {
    std::vector<CaptureChannel> out;
    out.reserve(decisions.size());
    for (const auto& decision : decisions) {
        if (decision.enabled) out.push_back(decision.channel);
    }
    return out;
}

std::string CaptureLaunchPlan::skippedSummary() const {
    std::string out;
    for (const auto& decision : decisions) {
        if (!decision.skipped) continue;
        if (!out.empty()) out += ", ";
        out += captureChannelName(decision.channel);
        out += ":";
        out += captureChannelSkipReasonName(decision.reason);
        if (!decision.detail.empty()) {
            out += "(";
            out += decision.detail;
            out += ")";
        }
    }
    return out;
}

bool captureChannelRuntimeReady(const CaptureConfig& config,
                                CaptureChannel channel,
                                std::string& detail) noexcept {
    detail.clear();
    if (config.symbols.empty() || config.symbols.front().empty()) {
        detail = "missing symbol";
        return false;
    }
#if HFTREC_WITH_CXET
    internal::ensureCxetInitialized();
    const ExchangeId exchange = exchangeIdFromConfig(config.exchange);
    if (exchange.raw == canon::kExchangeIdUnknown.raw) {
        detail = "unknown exchange";
        return false;
    }
    const canon::MarketType market = marketTypeFromConfig(exchange, config.market);
    if (market.raw == canon::kMarketTypeUnknown.raw) {
        detail = "unknown market";
        return false;
    }
    const auto stream = streamForCaptureChannel(channel);
    const auto caps = cxet::api::market::publicMarketDataCapabilities(exchange, market, stream);
    if (caps.selectedRuntimeReady()) return true;
    detail = "missing_mask=" + std::to_string(caps.missingMask);
    return false;
#else
    (void)channel;
    return true;
#endif
}

CaptureLaunchPlan buildCaptureLaunchPlan(const CaptureConfig& config,
                                          const std::vector<CaptureChannel>& requested,
                                          CaptureChannelAvailabilityFn availability,
                                          void* userData) {
    CaptureLaunchPlan plan{};
    plan.decisions.reserve(requested.size());
    if (availability == nullptr) availability = defaultAvailability;
    for (CaptureChannel channel : requested) {
        CaptureChannelDecision decision{};
        decision.channel = channel;
        decision.requested = true;
        std::string detail;
        if (availability(config, channel, detail, userData)) {
            decision.enabled = true;
        } else {
            decision.skipped = true;
            decision.reason = detail == "missing symbol" || detail == "unknown exchange" || detail == "unknown market"
                ? CaptureChannelSkipReason::InvalidConfig
                : CaptureChannelSkipReason::UnsupportedRoute;
            decision.detail = std::move(detail);
        }
        plan.decisions.push_back(std::move(decision));
    }
    return plan;
}

}  // namespace hftrec::capture
