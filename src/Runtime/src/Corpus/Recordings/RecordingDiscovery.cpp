#include "RecordingDiscovery.hpp"

#include "cxet/Api/Connector/ExchangeProductConnector.hpp"
#include "cxet/Api/Resolve/LocalSymbolValidation.hpp"
#include "cxet/Canon/CanonLookup.hpp"
#include "cxet/Canon/MarketMapping.hpp"
#include "cxet/Primitives/Buf/Symbol.hpp"

#include <limits.h>

namespace hftrec::recordings {
namespace {

int hexDigit(char ch) noexcept {
    if(ch>='0' && ch<='9') return ch-'0';
    if(ch>='A' && ch<='F') return ch-'A'+10;
    if(ch>='a' && ch<='f') return ch-'a'+10;
    return -1;
}

std::string decodedSymbol(std::string_view input,std::size_t maximum=Symbol::capacity-1u) {
    if(input.empty() || input.size()>3u*maximum) return {};
    std::string output;output.reserve(maximum);
    for(std::size_t i=0u;i<input.size();++i) {
        unsigned char ch=static_cast<unsigned char>(input[i]);
        if(ch=='%') {
            if(i+2u>=input.size()) return {};
            const auto high=hexDigit(input[i+1u]),low=hexDigit(input[i+2u]);
            if(high<0 || low<0) return {};
            ch=static_cast<unsigned char>((high<<4)|low);i+=2u;
        }
        if(ch<=0x20u || ch==0x7fu || ch=='/' || ch=='\\' || output.size()>=maximum) return {};
        output.push_back(static_cast<char>(ch));
    }
    return output;
}

std::string encodedSymbol(std::string_view symbol) {
    constexpr char hex[]="0123456789ABCDEF";
    std::string output;output.reserve(symbol.size()*3u);
    for(const auto value:symbol) {
        const auto ch=static_cast<unsigned char>(value);
        if((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') ||
           (ch>='0' && ch<='9') || ch=='_') output.push_back(value);
        else {output.push_back('%');output.push_back(hex[ch>>4u]);output.push_back(hex[ch&15u]);}
    }
    return output;
}

std::string lowerName(std::string_view input) {
    if(input.empty() || input.size()>=Symbol::capacity) return {};
    std::string output;output.reserve(input.size());
    for(auto ch:input) {
        if(ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
        if(!((ch>='a' && ch<='z') || (ch>='0' && ch<='9') || ch=='_' || ch=='-')) return {};
        output.push_back(ch);
    }
    return output;
}

bool resolvedSymbol(std::string_view exchangeText,std::string_view marketText,
                    std::string_view input,ExchangeId& exchange,
                    cxet::api::InstrumentRouteInfo& route) {
    const auto exchangeName=lowerName(exchangeText),marketName=lowerName(marketText);
    if(exchangeName.empty() || marketName.empty()) return false;
    exchange=canon::exchangeIdFromName(exchangeName.c_str());
    if(exchange.raw==canon::kExchangeIdUnknown.raw) return false;
    auto market=canon::marketTypeFromName(marketName.c_str());
    if(market.raw==canon::kMarketTypeUnknown.raw)
        market=canon::exchangeApiStringToCanonical(exchange,marketName.c_str());
    if(market.raw==canon::kMarketTypeUnknown.raw) return false;
    const auto text=decodedSymbol(input);Symbol symbol{};
    if(text.empty() || !symbol.copyFrom(text.c_str())) return false;
    const bool resolved=cxet::api::looksLikeLocalCryptoSymbol(symbol)
        ? cxet::api::resolveExchangeProductInstrumentRoute(exchange,market,symbol,&route)
        : cxet::api::resolveExchangeProductNativeInstrumentRoute(exchange,market,symbol,&route);
    return resolved && route.hasLocalSymbol && route.localSymbol.data[0]!='\0' &&
           route.identityEvidence!=cxet::api::InstrumentRouteIdentityEvidence::None;
}

}  // namespace

std::string recordingLocalSymbol(std::string_view input) noexcept {
    try {
        auto symbol=decodedSymbol(input);
        // Colon-delimited persisted local names precede the underscore grammar.
        for(auto& ch:symbol) if(ch==':') ch='_';
        return cxet::api::isLocalCryptoSymbolText(symbol.c_str())?symbol:std::string{};
    } catch(...) {return {};}
}

std::string recordingLocalSymbol(std::string_view exchangeText,std::string_view marketText,
                                 std::string_view input) noexcept {
    try {
        ExchangeId exchange{};cxet::api::InstrumentRouteInfo route{};
        if(!resolvedSymbol(exchangeText,marketText,input,exchange,route)) return {};
        return route.localSymbol.data;
    } catch(...) {return {};}
}

std::string recordingFolderSymbol(std::string_view input) noexcept {
    try {
        const auto symbol=decodedSymbol(input,NAME_MAX);
        if(symbol.empty()) return {};
        auto encoded=encodedSymbol(symbol);
        return encoded.size()>NAME_MAX?std::string{}:encoded;
    } catch(...) {return {};}
}

std::string recordingFolderSymbol(std::string_view exchange,std::string_view market,
                                  std::string_view input) noexcept {
    try {
        ExchangeId exchangeId{};cxet::api::InstrumentRouteInfo route{};
        if(!resolvedSymbol(exchange,market,input,exchangeId,route)) return {};
        // Recorder's FINAM folder identity retains the full proven native
        // ticker and board; route resolution stays with the exchange owner.
        if(exchangeId.raw==canon::kExchangeIdFinam.raw)
            return route.hasNativeSymbol && route.nativeSymbol.data[0]!='\0'
                ? encodedSymbol(route.nativeSymbol.data):std::string{};
        return encodedSymbol(route.localSymbol.data);
    } catch(...) {return {};}
}

}  // namespace hftrec::recordings
