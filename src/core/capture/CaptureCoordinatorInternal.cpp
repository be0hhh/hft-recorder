#include "core/capture/CaptureCoordinatorInternal.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>

#include "core/corpus/InstrumentMetadata.hpp"

#if HFTREC_WITH_CXET
#include "api/dispatch/BuildDispatch.hpp"
#include "api/env/CxetEnv.hpp"
#include "api/fields/RequestedFieldNames.hpp"
#include "canon/MarketMapping.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "composite/level_0/SubscribeObject.hpp"
#include "cxet.hpp"
#include "hft_trader/runtime/prep/SymbolMetadataRuntime.hpp"
#include "primitives/buf/Symbol.hpp"
#endif

namespace hftrec::capture::internal {

#if HFTREC_WITH_CXET
namespace {

bool symbolTextIsAll(std::string_view symbolText) noexcept {
    if (symbolText.size() != 3u) return false;
    char a = symbolText[0];
    char l0 = symbolText[1];
    char l1 = symbolText[2];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
    if (l0 >= 'A' && l0 <= 'Z') l0 = static_cast<char>(l0 + ('a' - 'A'));
    if (l1 >= 'A' && l1 <= 'Z') l1 = static_cast<char>(l1 + ('a' - 'A'));
    return a == 'a' && l0 == 'l' && l1 == 'l';
}
Symbol makeSymbol(const std::string& symbolText) noexcept {
    Symbol symbol{};
    symbol.copyFrom(symbolText.c_str());
    return symbol;
}

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

cxet::UnifiedRequestBuilder makeSubscribeBuilder(const CaptureConfig& config,
                                                 cxet::composite::out::SubscribeObject object) noexcept {
    auto symbol = makeSymbol(config.symbols.empty() ? std::string{} : config.symbols.front());
    const ExchangeId exchange = exchangeIdFromConfig(config.exchange);
    return cxet::subscribe()
        .object(object)
        .exchange(exchange)
        .market(marketTypeFromConfig(exchange, config.market))
        .api(normalizedApiSlot(config))
        .symbol(symbol);
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

void enrichInstrumentMetadataFromExchangeInfo(const CaptureConfig& config,
                                              corpus::InstrumentMetadata& metadata) noexcept {
#if HFTREC_WITH_CXET
    if (config.symbols.empty()) {
        metadata.metadataWarning = "hft_trader_metadata_skipped_empty_symbol";
        return;
    }
    hft_trader::runtime::SymbolMetadataResolveResult result{};
    const ExchangeId exchange = exchangeIdFromConfig(config.exchange);
    const bool ok = hft_trader::runtime::resolveSymbolMetadataOnce(exchange,
                                                                   marketTypeFromConfig(exchange, config.market),
                                                                   makeSymbol(config.symbols.front()),
                                                                   result);
    if (!ok) {
        metadata.metadataWarning = std::string{"hft_trader_metadata_failed:"} + (result.error.empty() ? "unknown" : result.error);
        return;
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
#else
    (void)config;
    metadata.metadataWarning = "hft_trader_metadata_unavailable_no_cxet";
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

#if HFTREC_WITH_CXET
cxet::UnifiedRequestBuilder makeTradesBuilder(const CaptureConfig& config) noexcept {
    return makeSubscribeBuilder(config, cxet::composite::out::SubscribeObject::Trades);
}

cxet::UnifiedRequestBuilder makeBookTickerBuilder(const CaptureConfig& config) noexcept {
    return makeSubscribeBuilder(config, cxet::composite::out::SubscribeObject::BookTicker);
}

cxet::UnifiedRequestBuilder makeLiquidationBuilder(const CaptureConfig& config) noexcept {
    const ExchangeId exchange = exchangeIdFromConfig(config.exchange);
    auto builder = cxet::subscribe()
        .object(cxet::composite::out::SubscribeObject::Liquidation)
        .exchange(exchange)
        .market(marketTypeFromConfig(exchange, config.market))
        .api(normalizedApiSlot(config));
    const std::string& symbolText = config.symbols.empty() ? std::string{} : config.symbols.front();
    if (symbolTextIsAll(symbolText)) return builder.symbol(canon::SlotScope::All);
    auto symbol = makeSymbol(symbolText);
    return builder.symbol(symbol);
}

cxet::UnifiedRequestBuilder makeOrderbookSubscribeBuilder(const CaptureConfig& config) noexcept {
    return makeSubscribeBuilder(config, cxet::composite::out::SubscribeObject::Orderbook);
}

bool applyRequestedAliases(const std::vector<std::string>& aliasNames,
                           cxet::UnifiedRequestBuilder& builder,
                           std::string& lastError) {
    if (aliasNames.empty()) {
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

    builder.aliases(Span<const canon::FieldId>(fieldIds, parsedCount));
    return true;
}
#endif

Status validateSupportedConfig(const CaptureConfig& config, std::string& lastError) {
    if (config.symbols.empty()) {
        lastError = "capture config must contain exactly one symbol";
        return Status::InvalidArgument;
    }
    if (config.symbols.size() != 1u) {
        lastError = "current capture path supports exactly one symbol per coordinator";
        return Status::InvalidArgument;
    }
#if HFTREC_WITH_CXET
    const ExchangeId exchange = exchangeIdFromConfig(config.exchange);
    if (exchange.raw == canon::kExchangeIdUnknown.raw) {
        lastError = "capture exchange must be one of: binance, bybit, kucoin, gate, bitget, aster, hyperliquid, okx, finam, mexc, xt, bingx, bitmart, toobit, htx, phemex";
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
