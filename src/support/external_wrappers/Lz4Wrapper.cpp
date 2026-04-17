#include "support/external_wrappers/Lz4Wrapper.hpp"

namespace hftrec::support {

Status lz4Encode(const std::uint8_t*, std::size_t,
                 std::uint8_t*, std::size_t,
                 int, std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

Status lz4Decode(const std::uint8_t*, std::size_t,
                 std::uint8_t*, std::size_t,
                 std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::support
