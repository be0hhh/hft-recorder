#include "core/capture/CaptureCoordinatorInternal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>

#include "core/corpus/InstrumentMetadata.hpp"
#include "core/finam/FinamEnvSync.hpp"

#if HFTREC_WITH_CXET
#include "api/dispatch/BuildDispatch.hpp"
#include "api/env/CxetEnv.hpp"
#include "api/fields/RequestedFieldNames.hpp"
#include "api/resolve/LocalSymbolValidation.hpp"
#include "canon/MarketMapping.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "cxet.hpp"
#include "hft_trader/runtime/prep/SymbolMetadataRuntime.hpp"
#include "hft_trader/runtime/config/RuntimeConfig.hpp"
#include "primitives/buf/Symbol.hpp"
#endif

namespace hftrec::capture::internal {

namespace {

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

bool cryptoCaptureExchange(std::string_view exchange) noexcept {
    return textEqualsAscii(exchange, "aster") ||
           textEqualsAscii(exchange, "backpack") ||
           textEqualsAscii(exchange, "binance") ||
           textEqualsAscii(exchange, "bingx") ||
           textEqualsAscii(exchange, "bitget") ||
           textEqualsAscii(exchange, "bitunix") ||
           textEqualsAscii(exchange, "bybit") ||
           textEqualsAscii(exchange, "coinbase") ||
           textEqualsAscii(exchange, "coinex") ||
           textEqualsAscii(exchange, "gate") ||
           textEqualsAscii(exchange, "htx") ||
           textEqualsAscii(exchange, "hyperliquid") ||
           textEqualsAscii(exchange, "kraken") ||
           textEqualsAscii(exchange, "kucoin") ||
           textEqualsAscii(exchange, "mexc") ||
           textEqualsAscii(exchange, "okx") ||
           textEqualsAscii(exchange, "phemex") ||
           textEqualsAscii(exchange, "poloniex") ||
           textEqualsAscii(exchange, "toobit") ||
           textEqualsAscii(exchange, "xt");
}

bool validLocalCryptoSymbol(std::string_view symbol) noexcept {
#if HFTREC_WITH_CXET
    std::string text{symbol};
    return cxet::api::isLocalCryptoSymbolText(text.c_str());
#else
    const std::size_t first = symbol.find('_');
    if (first == std::string_view::npos || first == 0u || first + 1u >= symbol.size()) return false;
    const std::size_t second = symbol.find('_', first + 1u);
    if (second != std::string_view::npos && (second == first + 1u || second + 1u >= symbol.size())) return false;
    if (second != std::string_view::npos && symbol.find('_', second + 1u) != std::string_view::npos) return false;
    for (char ch : symbol) {
        if (ch == '_') continue;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) continue;
        return false;
    }
    if (second != std::string_view::npos) {
        bool nonZeroMultiplier = false;
        bool greaterThanOne = false;
        for (std::size_t i = 0u; i < first; ++i) {
            if (symbol[i] < '0' || symbol[i] > '9') return false;
            nonZeroMultiplier = nonZeroMultiplier || symbol[i] != '0';
            greaterThanOne = greaterThanOne || symbol[i] > '1' || (symbol[i] == '1' && i + 1u < first);
        }
        if (!nonZeroMultiplier || !greaterThanOne) return false;
    }
    return true;
#endif
}

}  // namespace

#if HFTREC_WITH_CXET
namespace {

Symbol makeSymbol(std::string_view symbolText) noexcept {
    Symbol symbol{};
    char text[rawdata::SymbolMaxBytes]{};
    const std::size_t copyLen = std::min(symbolText.size(), sizeof(text) - 1u);
    for (std::size_t i = 0u; i < copyLen; ++i) text[i] = symbolText[i];
    symbol.copyFrom(text);
    return symbol;
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

}  // namespace
#endif

void ensureCxetInitialized() noexcept {
#if HFTREC_WITH_CXET
    static std::once_flag initOnce;
    std::call_once(initOnce, []() noexcept {
        cxet::initBuildDispatch();
    });
#endif
}

Status loadCaptureEnv(const CaptureConfig& config, std::string& lastError) noexcept {
#if HFTREC_WITH_CXET
    static std::mutex envMutex;
    std::lock_guard<std::mutex> lock(envMutex);

    const std::string primaryPath = config.envPath.empty() ? std::string{".env"} : config.envPath.string();
    bool loaded = cxet::loadDotEnv(primaryPath.c_str());
    if (!loaded && primaryPath == ".env") {
        static constexpr const char* kLegacyEnvPaths[] = {
            "apps/hft-recorder/.env",
            "apps/hft-trader/.env",
            "../.env",
            "../../.env",
            "../../../.env",
            "../../../../.env",
        };
        for (const char* path : kLegacyEnvPaths) {
            if (cxet::loadDotEnv(path)) {
                loaded = true;
                break;
            }
        }
    }

    if (!loaded && primaryPath != ".env") {
        lastError = "capture env file not found: " + primaryPath;
        return Status::InvalidArgument;
    }
    (void)cxet::initProxyFromEnv();
#else
    (void)config;
    (void)lastError;
#endif
    return Status::Ok;
}

bool finamConfigNeedsAccountId(const CaptureConfig& config) noexcept {
    return hftrec::finam::isFinamExchangeName(config.exchange)
        && !textEqualsAscii(config.market, "spot")
        && !textEqualsAscii(config.market, "shares");
}

Status refreshFinamAuthForConfig(const CaptureConfig& config,
                                 bool requireAccountId,
                                 std::string& lastError) noexcept {
    if (!hftrec::finam::isFinamExchangeName(config.exchange)) return Status::Ok;
    hftrec::finam::FinamEnvSyncRequest request{};
    request.envPath = config.envPath;
    request.apiSlot = normalizedApiSlot(config);
    request.requireAccountId = requireAccountId;
    request.mirrorStandardEnv = true;
    hftrec::finam::FinamEnvSyncResult result{};
    const auto status = hftrec::finam::refreshFinamEnvAndBearer(request, &result);
    if (!isOk(status)) {
        lastError = result.error.empty() ? "Finam auth preflight failed" : result.error;
    }
    return status;
}

Status persistFinamAuthForConfig(const CaptureConfig& config, std::string& lastError) noexcept {
    if (!hftrec::finam::isFinamExchangeName(config.exchange)) return Status::Ok;
    hftrec::finam::FinamEnvSyncRequest request{};
    request.envPath = config.envPath;
    request.apiSlot = normalizedApiSlot(config);
    request.requireAccountId = finamConfigNeedsAccountId(config);
    request.mirrorStandardEnv = true;
    hftrec::finam::FinamEnvSyncResult result{};
    const auto status = hftrec::finam::persistCurrentFinamBearer(request, &result);
    if (!isOk(status)) {
        lastError = result.error.empty() ? "Finam auth persist failed" : result.error;
    }
    return status;
}

bool enrichInstrumentMetadataFromExchangeInfo(const CaptureConfig& config,
                                              corpus::InstrumentMetadata& metadata) noexcept {
#if HFTREC_WITH_CXET
    if (config.symbols.empty()) {
        metadata.metadataWarning = "hft_trader_metadata_skipped_empty_symbol";
        return false;
    }
    hft_trader::runtime::SymbolMetadataResolveResult result{};
    const ExchangeId exchange = exchangeIdFromConfig(config.exchange);
    const bool ok = hft_trader::runtime::resolveSymbolMetadataOnce(exchange,
                                                                   marketTypeFromConfig(exchange, config.market),
                                                                   makeSymbol(primaryRouteSymbolText(config)),
                                                                   result,
                                                                   normalizedApiSlot(config),
                                                                   ApiProtocolProfile{});
    if (!ok) {
        metadata.metadataWarning = std::string{"hft_trader_metadata_failed:"} + (result.error.empty() ? "unknown" : result.error);
        return false;
    }
    metadata.tickSizeE8 = result.instrumentSpec.tickSizeRaw;
    metadata.tickSizeSource = "hft_trader_exchange_info";
    metadata.lotSizeE8 = result.instrumentSpec.stepSizeRaw;
    metadata.lotSizeSource = "hft_trader_exchange_info";
    if (result.instrumentSpec.contractBaseQtyRaw > 0) {
        metadata.contractBaseQtyE8 = result.instrumentSpec.contractBaseQtyRaw;
        metadata.contractBaseQtySource = "hft_trader_exchange_info";
    }
    if (result.instrumentSpec.priceBasisQtyRaw > 0) {
        metadata.priceBasisQtyE8 = result.instrumentSpec.priceBasisQtyRaw;
        metadata.priceBasisQtySource = "hft_trader_exchange_info";
    }
    if (result.instrumentSpec.expiryUtcNs > 0u &&
        result.instrumentSpec.expiryUtcNs <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        metadata.expiryUtcNs = static_cast<std::int64_t>(result.instrumentSpec.expiryUtcNs);
        metadata.expiryUtcNsSource = "hft_trader_exchange_info";
    }
    metadata.metadataSource = "hft_trader";
    metadata.metadataWarning.reset();
    return true;
#else
    (void)config;
    metadata.metadataWarning = "hft_trader_metadata_unavailable_no_cxet";
    return false;
#endif
}

std::int64_t nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

long long nowSec() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::uint8_t normalizedApiSlot(const CaptureConfig& config) noexcept {
    return config.apiSlot == 0u ? 1u : config.apiSlot;
}

std::string_view primaryIdentitySymbolText(const CaptureConfig& config) noexcept {
    if (config.symbols.empty()) return {};
    return config.symbols.front();
}

std::string_view primaryRouteSymbolText(const CaptureConfig& config) noexcept {
    return primaryIdentitySymbolText(config);
}

std::string_view routeSymbolTextAt(const CaptureConfig& config, std::size_t index) noexcept {
    if (index < config.symbols.size()) return config.symbols[index];
    return {};
}

#if HFTREC_WITH_CXET
bool validateRequestedAliases(const std::vector<std::string>& aliasNames,
                              std::string& lastError) {
    if (aliasNames.empty()) {
        lastError.clear();
        return true;
    }

    std::string commaSeparated;
    for (std::size_t i = 0; i < aliasNames.size(); ++i) {
        if (i != 0u) {
            commaSeparated += ',';
        }
        commaSeparated += aliasNames[i];
    }

    canon::FieldId fieldIds[cxet::kMaxRequestedTradeFields]{};
    const auto parsedCount = cxet::api::parseRequestedFieldNames(
        commaSeparated.c_str(),
        fieldIds,
        cxet::kMaxRequestedTradeFields);
    if (parsedCount == 0u) {
        lastError = "selected aliases did not resolve to CXET fields";
        return false;
    }

    lastError.clear();
    return true;
}

hft_trader::runtime::VenueRuntimeConfig makeTraderVenueConfig(const CaptureConfig& config) noexcept {
    hft_trader::runtime::VenueRuntimeConfig venue{};
    venue.name = config.exchange + "." + config.market;
    venue.exchange = exchangeIdFromConfig(config.exchange);
    venue.market = marketTypeFromConfig(venue.exchange, config.market);
    venue.apiSlot = normalizedApiSlot(config);
    venue.hasApiSlot = true;
    venue.marketEnabled = true;
    venue.userEnabled = false;
    venue.orderEnabled = false;
    venue.controlEnabled = false;
    for (std::size_t i = 0; i < config.symbols.size(); ++i) {
        const std::string_view routeSymbolText = routeSymbolTextAt(config, i);
        if (routeSymbolText.empty()) continue;
        Symbol symbol = makeSymbol(routeSymbolText);
        if (symbol.data[0] != '\0') venue.symbols.push_back(symbol);
    }
    hft_trader::runtime::setStrategyParam(venue.params, "reference_poll_interval_ms", "5000");
    return venue;
}
#endif

Status validateSupportedConfig(const CaptureConfig& config, std::string& lastError, bool allowMultiSymbol) {
    if (config.symbols.empty()) {
        lastError = "capture config must contain at least one symbol";
        return Status::InvalidArgument;
    }
    if (!allowMultiSymbol && config.symbols.size() != 1u) {
        lastError = "current capture path supports exactly one symbol per coordinator";
        return Status::InvalidArgument;
    }
    if (!config.routeSymbols.empty()) {
        lastError = "capture routeSymbols are no longer supported; use local symbols only";
        return Status::InvalidArgument;
    }
    if (const auto identityStatus = validateCryptoIdentitySymbols(config, lastError); !isOk(identityStatus)) return identityStatus;
#if HFTREC_WITH_CXET
    const ExchangeId exchange = exchangeIdFromConfig(config.exchange);
    if (exchange.raw == canon::kExchangeIdUnknown.raw) {
        lastError = "capture exchange must be one of: binance, bybit, kucoin, gate, bitget, aster, hyperliquid, okx, finam, mexc, xt, bingx, toobit, htx, phemex, poloniex";
        return Status::InvalidArgument;
    }
    if (marketTypeFromConfig(exchange, config.market).raw == canon::kMarketTypeUnknown.raw) {
        lastError = "capture market must be canonical or exchange API alias: futures, spot, shares, margin, inverse, swap, fapi, linear, usdt, linear-swap, usdt-m, usdm";
        return Status::InvalidArgument;
    }
#endif
    if (config.outputDir.empty()) {
        lastError = "capture output directory must not be empty";
        return Status::InvalidArgument;
    }
#if !HFTREC_WITH_CXET
    lastError = "hft-recorder was built without CXETCPP";
    return Status::Unimplemented;
#else
    return Status::Ok;
#endif
}

Status validateCryptoIdentitySymbols(const CaptureConfig& config, std::string& lastError) noexcept {
    if (!cryptoCaptureExchange(config.exchange)) return Status::Ok;
    for (const auto& symbol : config.symbols) {
        if (!validLocalCryptoSymbol(symbol)) {
            lastError = "capture crypto symbol must use local format BASE_QUOTE or N_BASE_QUOTE";
            return Status::InvalidArgument;
        }
    }
    return Status::Ok;
}

bool sessionConfigMatches(const CaptureConfig& lhs, const CaptureConfig& rhs) noexcept {
    return lhs.exchange == rhs.exchange
        && lhs.market == rhs.market
        && lhs.symbols == rhs.symbols
        && lhs.envPath == rhs.envPath
        && normalizedApiSlot(lhs) == normalizedApiSlot(rhs)
        && lhs.outputDir == rhs.outputDir
        && lhs.liveCacheMode == rhs.liveCacheMode;
}

}  // namespace hftrec::capture::internal
