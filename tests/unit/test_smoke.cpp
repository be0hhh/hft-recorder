#include <gtest/gtest.h>

#include "core/codec/Crc32c.hpp"
#include "core/codec/VarInt.hpp"
#include "core/codec/ZigZag.hpp"
#include "core/common/Status.hpp"

namespace {

TEST(Smoke, ScaffoldCompiles) {
    EXPECT_EQ(1 + 1, 2);
    EXPECT_EQ(hftrec::statusToString(hftrec::Status::Ok), "Ok");
}

TEST(Smoke, Crc32cReferenceVector) {
    // RFC 3720 appendix B.4 reference.
    const char* s = "123456789";
    auto crc = hftrec::codec::crc32c(reinterpret_cast<const std::uint8_t*>(s), 9);
    EXPECT_EQ(crc, 0xE3069283u);
}

TEST(Smoke, VarIntRoundTrip) {
    std::uint8_t buf[16];
    std::size_t written = 0;
    ASSERT_EQ(hftrec::codec::varintEncode(300u, buf, sizeof(buf), written), hftrec::Status::Ok);
    std::uint64_t got = 0;
    std::size_t consumed = 0;
    ASSERT_EQ(hftrec::codec::varintDecode(buf, written, consumed, got), hftrec::Status::Ok);
    EXPECT_EQ(got, 300u);
    EXPECT_EQ(consumed, written);
}

TEST(Smoke, ZigZagRoundTrip) {
    using namespace hftrec::codec;
    for (std::int64_t v : {0LL, 1LL, -1LL, 42LL, -42LL, 1LL << 40, -(1LL << 40)}) {
        EXPECT_EQ(zigzagDecode(zigzagEncode(v)), v);
    }
}

}  // namespace
