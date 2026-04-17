#pragma once

#include <string>
#include <vector>

#include "core/validation/ValidationResult.hpp"

namespace hftrec::validation {

class ValidationRunner {
  public:
    ValidationResult compare(const std::vector<std::string>& original,
                             const std::vector<std::string>& decoded) const;
};

}  // namespace hftrec::validation
