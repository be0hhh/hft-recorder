#include "core/capture/SessionId.hpp"

#include <cstdio>

namespace hftrec::capture {

std::string makeSessionId(const std::string& exchange,
                          const std::string& market,
                          const std::string& symbolOrBasket,
                          long long timestampSuffix) noexcept {
    char buffer[160]{};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%lld_%s_%s_%s",
                  timestampSuffix,
                  exchange.c_str(),
                  market.c_str(),
                  symbolOrBasket.c_str());
    return std::string{buffer};
}

}  // namespace hftrec::capture
