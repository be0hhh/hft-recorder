#pragma once

#include <string>

namespace hftrec::capture {

std::string makeSessionId(const std::string& exchange,
                          const std::string& market,
                          const std::string& symbolOrBasket,
                          long long unixSeconds) noexcept;

}  // namespace hftrec::capture
