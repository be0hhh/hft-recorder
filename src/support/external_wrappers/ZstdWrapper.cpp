#include "support/external_wrappers/ZstdWrapper.hpp"

namespace hftrec::support {

Status zstdEncode(const std::uint8_t*, std::size_t,
                  std::uint8_t*, std::size_t,
                  int, std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

Status zstdDecode(const std::uint8_t*, std::size_t,
                  std::uint8_t*, std::size_t,
                  std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::support
