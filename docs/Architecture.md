# hft-recorder architecture

## Ownership

Recorder owns capture sessions, canonical corpus files, validation, replay
views and compression-lab results. It consumes exchange-normalized public data;
it does not own venue wire grammar, Parser topology, trading execution or
Backtest accounting.

## Direct source graph

The CXET root composes Recorder with direct target edges:

~~~text
cxet::cxet_lib ────────────────> hft-recorder
Parser producer-owned client ──> hft-recorder
hft-compressor targets ────────> hft-recorder
hftrec corpus contract ────────> hft-recorder / hft-backtest
~~~

These are compile-time source/target dependencies. They are not installed SDKs,
copied headers, imported sibling binaries or runtime network hops.

## Layers

### Capture

- owns session lifecycle and hard duration/byte limits;
- receives normalized events through public CXET or Parser-owned contracts;
- materializes Recorder-owned rows;
- writes canonical session/corpus outputs;
- records gaps, drops and terminal seal evidence.

### Corpus

- direct Recorder capture uses normalized JSON session files;
- Parser-wide capture uses the sealed sharded binary corpus;
- "/mnt/d/recordings" is the active WSL source of truth when present;
- hot cache and durable storage retain the same event meaning.

### Replay and validation

Readers reconstruct ordered normalized streams for validation, charts,
compression and Backtest adapters. Missing or inconsistent required input fails
closed; it is not replaced with a similar stream.

### Compression lab

The lab compares zstd, lz4, brotli and xz/lzma baselines with stream-specific
custom variants. Rankings are per stream family. Every candidate must decode
losslessly to the canonical corpus meaning.

### GUI

C++ owns capture, corpus I/O, validation, lab execution and models. QML owns
presentation and operator interaction; it does not own file or exchange logic.

## Public boundary

Recorder may consume public normalized CXET contracts and the directly compiled
producer/corpus targets declared by the root graph. It must not include CXET
"network/", "parse/", "exchanges/" or private runtime internals.

Logical streams remain explicit. A live trade stream is not silently replaced
with a historical aggregate-trade stream, and tri-state/unknown event meaning
is never fabricated to fit a narrower consumer.
