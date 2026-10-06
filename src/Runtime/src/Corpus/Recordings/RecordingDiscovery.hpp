#pragma once

#include <string>
#include <string_view>

namespace hftrec::recordings {

// Bounded cold naming surface. No directory discovery, migration or I/O.
std::string recordingLocalSymbol(std::string_view symbol) noexcept;
std::string recordingLocalSymbol(std::string_view exchange,std::string_view market,
                                 std::string_view symbol) noexcept;
std::string recordingFolderSymbol(std::string_view symbol) noexcept;
std::string recordingFolderSymbol(std::string_view exchange,std::string_view market,
                                  std::string_view symbol) noexcept;

}  // namespace hftrec::recordings
