#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/common/Status.hpp"
#include "variants/orderbook/var01_raw_updates_cpp/decode.hpp"
#include "variants/orderbook/var01_raw_updates_cpp/encode.hpp"

namespace {

using hftrec::Status;
using hftrec::isOk;
using hftrec::variants::orderbook_var01::encodeBlock;
using hftrec::variants::orderbook_var01::decodeBlock;

TEST(IdentityCodec, RoundTripEmpty) {
    std::uint8_t encodeBuf[1]{};
    std::size_t encoded = 999;
    EXPECT_EQ(encodeBlock(nullptr, 0, encodeBuf, sizeof(encodeBuf), encoded), Status::Ok);
    EXPECT_EQ(encoded, 0u);

    std::uint8_t decodeBuf[1]{};
    std::size_t decoded = 999;
    EXPECT_EQ(decodeBlock(encodeBuf, 0, decodeBuf, sizeof(decodeBuf), decoded), Status::Ok);
    EXPECT_EQ(decoded, 0u);
}

TEST(IdentityCodec, RoundTripKnownPayload) {
    const std::string payload = "hft-recorder identity roundtrip sample " + std::string(64, 'A');
    std::vector<std::uint8_t> encoded(payload.size() + 32, 0xCD);
    std::size_t encodedLen = 0;

    ASSERT_EQ(encodeBlock(reinterpret_cast<const std::uint8_t*>(payload.data()),
                          payload.size(), encoded.data(), encoded.size(), encodedLen),
              Status::Ok);
    EXPECT_EQ(encodedLen, payload.size());
    EXPECT_EQ(std::memcmp(encoded.data(), payload.data(), payload.size()), 0);

    std::vector<std::uint8_t> decoded(payload.size(), 0);
    std::size_t decodedLen = 0;
    ASSERT_EQ(decodeBlock(encoded.data(), encodedLen, decoded.data(), decoded.size(),
                          decodedLen), Status::Ok);
    EXPECT_EQ(decodedLen, payload.size());
    EXPECT_EQ(std::memcmp(decoded.data(), payload.data(), payload.size()), 0);
}

TEST(IdentityCodec, OutOfRangeOnSmallOutputBuffer) {
    const std::uint8_t in[16] = {0};
    std::uint8_t out[8]{};
    std::size_t written = 0;
    EXPECT_EQ(encodeBlock(in, sizeof(in), out, sizeof(out), written), Status::OutOfRange);
    EXPECT_EQ(written, 0u);
}

}  // namespace
