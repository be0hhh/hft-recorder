#include "core/capture/CaptureCoordinatorInternal.hpp"

#include <chrono>
#include <mutex>

#include "api/dispatch/BuildDispatch.hpp"
#include "api/env/CxetEnv.hpp"
#include "api/fields/RequestedFieldNames.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "composite/level_0/SubscribeObject.hpp"
#include "cxet.hpp"
#include "primitives/buf/Symbol.hpp"

namespace hftrec::capture::internal {

namespace {

Symbol makeSymbol(const std::string& symbolText) noexcept {
    Symbol symbol{};
    symbol.copyFrom(symbolText.c_str());
    return symbol;
}

}  // namespace

void ensureCxetInitialized() noexcept {
    static std::once_flag initOnce;
    std::call_once(initOnce, []() noexcept {
        (void)cxet::loadDotEnv(".env");
        (void)cxet::initProxyFromEnv();
        cxet::initBuildDispatch();
    });
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

cxet::UnifiedRequestBuilder makeTradesBuilder(const std::string& symbolText) noexcept {
    auto symbol = makeSymbol(symbolText);
    return cxet::subscribe()
        .object(cxet::composite::out::SubscribeObject::Trades)
        .exchange(canon::kExchangeIdBinance)
        .market(canon::kMarketTypeFutures)
        .symbol(symbol);
}

cxet::UnifiedRequestBuilder makeBookTickerBuilder(const std::string& symbolText) noexcept {
    auto symbol = makeSymbol(symbolText);
    return cxet::subscribe()
        .object(cxet::composite::out::SubscribeObject::BookTicker)
        .exchange(canon::kExchangeIdBinance)
        .market(canon::kMarketTypeFutures)
        .symbol(symbol);
}

cxet::UnifiedRequestBuilder makeOrderbookSubscribeBuilder(const std::string& symbolText) noexcept {
    auto symbol = makeSymbol(symbolText);
    return cxet::subscribe()
        .object(cxet::composite::out::SubscribeObject::Orderbook)
        .exchange(canon::kExchangeIdBinance)
        .market(canon::kMarketTypeFutures)
        .symbol(symbol);
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

Status validateSupportedConfig(const CaptureConfig& config, std::string& lastError) {
    if (config.symbols.empty()) {
        lastError = "capture config must contain exactly one symbol";
        return Status::InvalidArgument;
    }
    if (config.symbols.size() != 1u) {
        lastError = "current capture path supports exactly one symbol per coordinator";
        return Status::InvalidArgument;
    }
    if (config.exchange != kSupportedExchange) {
        lastError = "current capture path supports exchange=binance only";
        return Status::InvalidArgument;
    }
    if (config.market != kSupportedMarket) {
        lastError = "current capture path supports market=futures_usd only";
        return Status::InvalidArgument;
    }
    if (config.outputDir.empty()) {
        lastError = "capture output directory must not be empty";
        return Status::InvalidArgument;
    }
    return Status::Ok;
}

bool sessionConfigMatches(const CaptureConfig& lhs, const CaptureConfig& rhs) noexcept {
    return lhs.exchange == rhs.exchange
        && lhs.market == rhs.market
        && lhs.symbols == rhs.symbols
        && lhs.outputDir == rhs.outputDir;
}

}  // namespace hftrec::capture::internal
