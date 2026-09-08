# hft-recorder - comparison matrix

## Purpose

This document defines what must be compared.

The matrix exists to prevent benchmark drift and selective reporting.

## Stream families

- `trade_like` (`aggTrade` where that is the real semantics)
- `l1_bookticker`
- `orderbook_updates`

## Method families

### A. Standard baselines
- raw binary + `zstd`
- raw binary + `lz4`
- raw binary + `gzip`
- raw binary + `brotli`
- raw binary + `lzma/xz`

### B. Hybrid baselines
- delta + `zstd`
- delta + `lz4`
- delta + `gzip`
- columnar + `zstd`
- columnar + `lz4`
- delta + varint

### C. Python prototypes
- one or more transform prototypes per stream family
- one or more orderbook representation prototypes

### D. C++ custom candidates
- promoted winners from Python or directly implemented custom ideas

## Required metrics

For every matrix cell record:
- `input_bytes`
- `output_bytes`
- `compression_ratio`
- `encode_throughput`
- `decode_throughput`
- `online_feasible`
- `notes`

For orderbook also record:
- `representation_bytes_before_codec`
- `reconstruction_complexity`
- `gap_recovery_cost`

## Expected reporting style

Results must be summarized in three ways:
- per-stream winner
- best online winner
- best archive/replay winner

No single universal winner is required.
