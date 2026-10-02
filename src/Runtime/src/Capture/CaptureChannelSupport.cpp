#include "CaptureChannelSupport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <new>
#include <string_view>
#include <thread>
#include <utility>

#if HFTREC_WITH_CXET
#include "cxet/Api/Market/PublicMarketCatalog.hpp"
#include "cxet/Api/Instrument/InstrumentDenomination.hpp"
#include "cxet/Api/Resolve/LocalSymbolValidation.hpp"
#include "cxet/Canon/MarketMapping.hpp"
#include "cxet/Canon/PositionAndExchange.hpp"
#include "cxet/Canon/Subtypes.hpp"
#include "CaptureCoordinatorInternal.hpp"
#include "CaptureCoordinatorRuntimeHelpers.hpp"

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
    if (textEqualsAscii(exchange, "mexc")) return canon::kExchangeIdMexc;
    if (textEqualsAscii(exchange, "xt")) return canon::kExchangeIdXt;
    if (textEqualsAscii(exchange, "bingx")) return canon::kExchangeIdBingx;
    if (textEqualsAscii(exchange, "toobit")) return canon::kExchangeIdToobit;
    if (textEqualsAscii(exchange, "htx")) return canon::kExchangeIdHtx;
    if (textEqualsAscii(exchange, "phemex")) return canon::kExchangeIdPhemex;
    if (textEqualsAscii(exchange, "poloniex")) return canon::kExchangeIdPoloniex;
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

cxet::api::market::PublicMarketObject objectForCaptureChannel(CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades:return cxet::api::market::PublicMarketObject::Trade;
        case CaptureChannel::BookTicker:return cxet::api::market::PublicMarketObject::BookTicker;
        case CaptureChannel::Orderbook:return cxet::api::market::PublicMarketObject::Depth;
    }
    return {};
}

#endif

bool defaultAvailability(const CaptureConfig& config,
                         CaptureChannel channel,
                         std::string& detail,
                         void*) {
    return captureChannelRuntimeReady(config, channel, detail);
}

CaptureChannelSkipReason skipReasonForUnavailableDetail(std::string_view detail) noexcept {
    return detail == "missing symbol" || detail == "unknown exchange" || detail == "unknown market"
        ? CaptureChannelSkipReason::InvalidConfig
        : CaptureChannelSkipReason::UnsupportedRoute;
}

CaptureChannelDecision enabledDecision(CaptureChannel channel) {
    CaptureChannelDecision decision{};
    decision.channel = channel;
    decision.requested = true;
    decision.enabled = true;
    return decision;
}

CaptureChannelDecision skippedDecision(CaptureChannel channel,
                                       CaptureChannelSkipReason reason,
                                       std::string detail) {
    CaptureChannelDecision decision{};
    decision.channel = channel;
    decision.requested = true;
    decision.skipped = true;
    decision.reason = reason;
    decision.detail = std::move(detail);
    return decision;
}

#if HFTREC_WITH_CXET
CaptureLaunchPlan envPreflightFailedPlan(const std::vector<CaptureChannel>& requested,
                                         std::string detail) {
    CaptureLaunchPlan plan{};
    plan.decisions.reserve(requested.size());
    if (detail.empty()) detail = "capture env preflight failed";
    for (CaptureChannel channel : requested) {
        plan.decisions.push_back(
            skippedDecision(channel, CaptureChannelSkipReason::ApplyFailed, detail));
    }
    return plan;
}


#endif

}  // namespace

const char* captureChannelName(CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades: return "trades";
        case CaptureChannel::BookTicker: return "bookticker";
        case CaptureChannel::Orderbook: return "orderbook";
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
    if (channel == CaptureChannel::Orderbook) {
        detail = "native book capture requires an exact binary source directory and configured byte quota; JSON cannot preserve snapshot/rebase";
        return false;
    }
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
    auto registry=std::unique_ptr<cxet::api::market::PublicMarketRegistry>(new(std::nothrow) cxet::api::market::PublicMarketRegistry{});
    if (!registry || cxet::api::market::buildStagedPublicMarketCatalog(registry.get())!=cxet::api::market::StagedPublicMarketCatalogStatus::Ready ||
        !registry->select(exchange.raw,market.raw,objectForCaptureChannel(channel),0u,0u)) {
        detail="registered native market descriptor unavailable";return false;
    }
    return true;
#else
    (void)channel;
    return true;
#endif
}

#if HFTREC_WITH_CXET
bool makeConfiguredCaptureSource(const CaptureConfig& config,CaptureChannel channel,
    cxet::runtime::market::ConfiguredMarketSource& out,std::string& detail) noexcept {
    if (!captureChannelRuntimeReady(config,channel,detail)) return false;
    const auto identity=internal::primaryIdentitySymbolText(config);
    const auto routeText=internal::primaryRouteSymbolText(config);
    if (identity.empty() || identity.size()>=Symbol::capacity || routeText.empty() || routeText.size()>=Symbol::capacity) {
        detail="invalid exact capture symbol";return false;
    }
    out={};out.exchange=exchangeIdFromConfig(config.exchange);out.market=marketTypeFromConfig(out.exchange,config.market);
    out.object=objectForCaptureChannel(channel);out.apiSlot=internal::normalizedApiSlot(config);
    out.symbol.copyFrom(std::string{routeText}.c_str());
    cxet::api::InstrumentRouteInfo route{};
    if (cxet::api::looksLikeLocalCryptoSymbol(out.symbol)) {
        if (!cxet::api::resolveExchangeProductInstrumentRoute(out.exchange,out.market,out.symbol,&route,out.apiProtocolProfile) ||
            !route.hasNativeSymbol || route.baseMultiplier!=1u) {
            detail="capture instrument requires an authoritative denomination binding";return false;
        }
    }
    cxet::api::instrument::InstrumentDenominationTransform transform{};
    const auto kind=out.market==canon::kMarketTypeSpot || out.market==canon::kMarketTypeMargin
        ?cxet::api::instrument::InstrumentQuantityKind::SpotBase:cxet::api::instrument::InstrumentQuantityKind::DerivativeContracts;
    if (!cxet::api::instrument::makeInstrumentDenominationTransform(1u,1u,kind,&transform) ||
        !cxet::api::instrument::runtimeInstrumentNumeric(transform,&out.numeric)) {
        detail="capture native numeric context unavailable";return false;
    }
    return true;
}
#endif

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
            decision = enabledDecision(channel);
        } else {
            decision = skippedDecision(channel, skipReasonForUnavailableDetail(detail), std::move(detail));
        }
        plan.decisions.push_back(std::move(decision));
    }
    return plan;
}

CaptureLaunchPlan preflightCaptureLaunchPlan(const CaptureConfig& config,
                                             const std::vector<CaptureChannel>& requested) {
    CaptureLaunchPlan plan{};
    plan.decisions.reserve(requested.size());
#if HFTREC_WITH_CXET
    std::string envError;
    if (const Status envStatus = internal::loadCaptureEnv(config, envError); !isOk(envStatus)) {
        std::string detail = envError;
        if (detail.empty()) {
            detail = "capture env preflight failed: ";
            detail += std::string{statusToString(envStatus)};
        }
        return envPreflightFailedPlan(requested, std::move(detail));
    }
#endif
    for (CaptureChannel channel : requested) {
        std::string detail;
        if (!captureChannelRuntimeReady(config, channel, detail)) {
            plan.decisions.push_back(
                skippedDecision(channel, skipReasonForUnavailableDetail(detail), std::move(detail)));
            continue;
        }

        plan.decisions.push_back(enabledDecision(channel));
    }
    return plan;
}

}  // namespace hftrec::capture
