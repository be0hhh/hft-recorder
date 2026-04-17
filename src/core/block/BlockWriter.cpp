#include "core/block/BlockWriter.hpp"

// ─── Implementation guide for Phase 2 ────────────────────────────────────────
//
// BlockWriter is the write-side of doc/FILE_FORMAT.md. It owns one file
// descriptor, appends event payloads into an in-memory block buffer, and
// flushes when ANY of the three thresholds fires:
//
//     events >= constants::kBlockMaxEvents
//  || wall_elapsed_ms >= constants::kBlockMaxWallMs
//  || buffered_bytes >= constants::kBlockMaxBytes
//
// On flush:
//   1. Build BlockHeader with event_count, payload_size, first_ts_ns, flags,
//      crc32c(payload).
//   2. pwrite(fd, &hdr, 32, offset); pwrite(fd, payload, payload_size, offset + 32).
//   3. offset += 32 + payload_size; buffered counters reset to 0.
//   4. Every constants::kCoderResetEveryBlocks blocks, set flags |= kFlagCoderReset
//      and tell the upstream codec to reset its state. See doc/ARITHMETIC_CODING.md.
//   5. Every constants::kFsyncEveryBlocks blocks, call fsync(fd). Log slow fsyncs
//      (> 50 ms) at warn level via hftrec::log::get("writer.<stream>").
//
// On open:
//   1. Write FileHeader (64 B) to offset 0; include magic, version, stream,
//      codec, created_ns, symbol, exchange, and CRC32C over bytes 0..59.
//   2. offset = 64.
//
// On close:
//   1. If the current block is non-empty, flush it with flags |= kFlagCoderReset
//      (clean EOF marker).
//   2. fsync, close(fd).
//
// Hard rules:
//   - No exceptions; return Status on every failure.
//   - Allocate the block buffer once in open(), not per appendEvent.
//   - Use pwrite, not write — offset is explicit so we can reason about recovery.
//   - errno on failure goes into the metrics label `errno` (see LOGGING_AND_METRICS).
//
// Phase 1 status: all methods return Unimplemented until the recorder is wired.
// ─────────────────────────────────────────────────────────────────────────────

namespace hftrec {

BlockWriter::~BlockWriter() = default;

Status BlockWriter::open(const WriterConfig& cfg) noexcept {
    cfg_ = cfg;
    return Status::Unimplemented;
}

Status BlockWriter::appendEvent(const std::uint8_t* payload, std::size_t len) noexcept {
    (void)payload;
    (void)len;
    return Status::Unimplemented;
}

Status BlockWriter::flush() noexcept {
    return Status::Unimplemented;
}

Status BlockWriter::close() noexcept {
    return Status::Unimplemented;
}

}  // namespace hftrec
