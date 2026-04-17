#include "support/external_wrappers/XzWrapper.hpp"

namespace hftrec::support {

Status xzEncode(const std::uint8_t*, std::size_t,
                std::uint8_t*, std::size_t,
                int, std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

Status xzDecode(const std::uint8_t*, std::size_t,
                std::uint8_t*, std::size_t,
                std::size_t& out_written) noexcept {
    out_written = 0;
    return Status::Unimplemented;
}

}  // namespace hftrec::support
