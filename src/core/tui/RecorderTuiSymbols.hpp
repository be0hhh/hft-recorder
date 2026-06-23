#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/tui/RecorderTuiPreset.hpp"

namespace hftrec::tui {

struct RecorderTuiVenueSpec {
    const char* key;
    const char* label;
    const char* exchange;
    const char* market;
};

struct SymbolBatchInput {
    std::vector<std::string> symbols{};
    std::vector<std::filesystem::path> loadedFiles{};
};

const std::vector<RecorderTuiVenueSpec>& allCryptoVenueSpecs();
const RecorderTuiVenueSpec* venueSpecByKey(std::string_view key) noexcept;

std::filesystem::path symbolListConfigDir();
std::string venueSymbolsFromGlobalInput(std::string_view venueKey, std::string_view symbolsText);
std::string renderSymbolListText(const std::vector<std::string>& symbols);

bool loadSymbolBatchInput(std::string_view text,
                          const std::filesystem::path& listDir,
                          SymbolBatchInput& out,
                          std::string& error);

std::vector<RecorderTuiJob> generateJobsForSymbols(const std::vector<std::string>& symbols,
                                                   const std::vector<RecorderTuiVenueSpec>& venues,
                                                   std::size_t startIndex);

}  // namespace hftrec::tui
