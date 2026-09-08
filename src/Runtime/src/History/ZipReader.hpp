#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hftrec::history {

bool extractSingleFileZip(const std::vector<std::uint8_t>& zip,
                          std::string& fileName,
                          std::string& text,
                          std::string& error);

}  // namespace hftrec::history
