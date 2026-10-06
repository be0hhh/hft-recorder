#pragma once

#include <filesystem>

namespace hftrec::recordings {

// Cold path selection only; these functions never create or move recordings.
std::filesystem::path defaultRecordingsRoot() noexcept;
std::filesystem::path normalizeRecordingsPath(const std::filesystem::path& path) noexcept;
std::filesystem::path normalizeExplicitRecordingsPath(const std::filesystem::path& path) noexcept;

}  // namespace hftrec::recordings
