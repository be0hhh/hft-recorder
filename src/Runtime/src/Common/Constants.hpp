#pragma once

#include <cstddef>
#include <cstdint>

// hft-recorder — compile-time constants.
// Values are defaults; env overrides are resolved at startup (see CONFIG_AND_CLI.md).

namespace hftrec::constants {

// Block flush policy — min(events, wall-time, bytes).
inline constexpr std::size_t kBlockMaxEvents      = 512;
inline constexpr std::uint32_t kBlockMaxWallMs    = 1000;
inline constexpr std::size_t kBlockMaxBytes       = 256u * 1024u;

// Coder reset cadence.
inline constexpr std::size_t kCoderResetEveryBlocks = 1024;

// fsync cadence (writer side).
inline constexpr std::size_t kFsyncEveryBlocks      = 16;

// Snapshot cadence (REST polling for orderbook full snapshot).
inline constexpr std::uint32_t kSnapshotIntervalSec = 60;

// SPSC ring capacity per stream (power-of-two).
inline constexpr std::size_t kSpscCapacityTrades     = 2048;
inline constexpr std::size_t kSpscCapacityBookTicker = 2048;
inline constexpr std::size_t kSpscCapacityDepth      = 8192;
inline constexpr std::size_t kSpscCapacitySnapshot   = 16;

// Prometheus push interval (seconds).
inline constexpr std::uint32_t kMetricsPushIntervalSec = 10;

// File format.
inline constexpr std::uint32_t kFileMagic       = 0x43585243u;  // 'CXRC'
inline constexpr std::uint16_t kFileVersion     = 1;
inline constexpr std::size_t   kFileHeaderBytes = 64;
inline constexpr std::uint32_t kBlockMagic      = 0x004B4C42u;  // 'BLK\0'
inline constexpr std::size_t   kBlockHeaderBytes = 32;

// Block header flag bits.
inline constexpr std::uint8_t kFlagCoderReset = 0x01;
inline constexpr std::uint8_t kFlagHasGap     = 0x02;

}  // namespace hftrec::constants
