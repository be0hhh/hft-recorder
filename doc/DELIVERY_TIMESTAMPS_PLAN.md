# Delivery timestamps and corpus v4 plan

Status: proposed, static-only. The current durable corpus remains the v3
contract documented in `SESSION_CORPUS_FORMAT.md`.

## Goal

Record enough information to reproduce the order in which the recorder
observed market events without pretending that exchange timestamps form one
global delivery axis.

The future schema carries three different facts:

- `tsNs`: source/exchange event time already present in the normalized event;
- `captureWallNs`: local epoch time at the recorder capture boundary;
- `captureMonoNs`: local monotonic time at the same boundary;
- `ingestSeq`: strict session-global tie-breaker assigned by the recorder.

These fields have different meanings and must never be silently substituted for
one another.

## Current truth

The live capture path currently translates CXET RuntimeV1 payloads through
`src/core/cxet_bridge/CxetCaptureBridge.*`, assigns `captureSeq` and
`ingestSeq` in the capture runtime, serializes canonical JSONL, and loads it
through recorder-owned replay/corpus types.

Current v3 trade and BBO rows persist:

```text
tsNs, captureSeq, ingestSeq, normalized price/quantity/side fields
```

`captureSeq` is per channel. `ingestSeq` is session-global. `tsNs` is the
canonical replay bucket timestamp today. Neither sequence is a clock.

The current bridge receives a decoded event and stream metadata but no paired
local wall/monotonic observation. Taking a new clock reading later in the writer
would measure writer backlog, not the capture boundary.

## Normative clock semantics

### `tsNs`

`tsNs` remains the exchange/source timestamp for compatibility. It is not
renamed in the durable v4 JSON schema.

- It may be absent or zero when the source does not provide it.
- Precision and processing-stage meaning are exchange/stream specific.
- BBO and trades from one venue are not assumed to share a total order.
- A negative `captureWallNs - tsNs` is clock evidence, not a latency value to
  clamp to zero.

### `captureWallNs`

`captureWallNs` is `CLOCK_REALTIME` nanoseconds sampled when the recorder-owned
capture callback accepts the normalized event.

It is not named `receiveTimestampNs` because the recorder does not currently
own the CXET socket/frame receive boundary. It includes any upstream parse,
publication and callback delivery time.

Use cases:

- approximate exchange-to-recorder delay when clock quality is acceptable;
- aligning a session with external epoch-timestamped evidence;
- detecting clock steps and impossible negative one-way samples.

### `captureMonoNs`

`captureMonoNs` is `CLOCK_MONOTONIC_RAW` nanoseconds sampled at the same capture
boundary.

Use cases:

- deterministic within-session arrival ordering;
- inter-arrival and cross-stream delay measurements;
- replay pacing immune to wall-clock corrections;
- local backlog and stall detection.

Its absolute value is session-local and must not be compared across hosts,
boots, or sessions.

### `captureSeq` and `ingestSeq`

`captureSeq` remains strictly increasing within one persisted logical channel.

`ingestSeq` remains strictly increasing across every persisted event in one
session. For two rows with equal `captureMonoNs`, replay order is `ingestSeq`.
The writer must assign the sequence in the same acceptance operation as the
capture stamp; a later writer-thread ordering is not equivalent.

## Proposed capture contract

The capture owner samples both clocks once and passes the immutable observation
through the bridge and queues:

```cpp
struct CaptureObservationV1 {
  std::uint64_t captureWallNs{0u};
  std::uint64_t captureMonoNs{0u};
  std::uint64_t captureSeq{0u};
  std::uint64_t ingestSeq{0u};
};
```

Proposed hot/capture-facing row tail:

```cpp
struct CapturedTradeRow {
  // Existing normalized fields remain first.
  std::uint64_t eventId{0u};
  std::uint64_t tsNs{0u};
  std::uint64_t captureWallNs{0u};
  std::uint64_t captureMonoNs{0u};
  std::uint64_t captureSeq{0u};
  std::uint64_t ingestSeq{0u};
};

struct CapturedBookTickerRow {
  // Existing normalized fields remain first.
  std::uint64_t eventId{0u};
  std::uint64_t tsNs{0u};
  std::uint64_t captureWallNs{0u};
  std::uint64_t captureMonoNs{0u};
  std::uint64_t captureSeq{0u};
  std::uint64_t ingestSeq{0u};
};
```

The real implementation should avoid adding sequence fields twice if the
owning capture row already carries `runtime::EventSequenceIds`. The normative
requirement is one immutable observation object, not this exact physical
layout.

The bridge API becomes explicit about ownership:

```cpp
static CapturedTradeRow captureTrade(
    const cxet::composite::TradeRuntimeV1& trade,
    const cxet::composite::StreamMeta& meta,
    CaptureObservationV1 observation);

static CapturedBookTickerRow captureBookTicker(
    const cxet::composite::BookTickerRuntimeV1& bookTicker,
    const cxet::composite::StreamMeta& meta,
    CaptureObservationV1 observation);
```

The bridge copies clocks; it does not call `clock_gettime`, allocate sequence
IDs, or infer missing exchange time. The capture coordinator owns the clock and
sequence operation.

## Durable v4 rows

The v4 JSON fields for every live market-event row are:

```json
{
  "eventId": 123,
  "tsNs": 1713168000000000000,
  "captureWallNs": 1713168000001250000,
  "captureMonoNs": 8123456789012,
  "captureSeq": 42,
  "ingestSeq": 305
}
```

Stream-specific normalized fields follow the existing schema. Numeric values
remain JSON integers. No floating point or humanized duration is canonical.

Rules:

- `eventId` retains the normalized RuntimeV1 identity when available; zero
  means unavailable, not sequence zero.
- `captureWallNs` and `captureMonoNs` are both positive for native live v4
  capture rows.
- historical/imported rows may lack both capture clocks, but must declare a
  non-live origin and cannot claim captured-arrival replay support.
- synthesized timestamps are forbidden in canonical live rows.
- optional absence is represented by schema/origin capability, not by silently
  copying `tsNs` into a capture field.

## Manifest clock evidence

The v4 manifest adds one session-level object:

```json
{
  "capture_clock": {
    "wall_clock": "CLOCK_REALTIME",
    "monotonic_clock": "CLOCK_MONOTONIC_RAW",
    "capture_boundary": "recorder_normalized_event_accept",
    "wall_sync_status": "unknown",
    "wall_sync_max_error_ns": null,
    "start_wall_ns": 1713168000000000000,
    "start_mono_ns": 8123450000000,
    "end_wall_ns": 1713168060000000000,
    "end_mono_ns": 8183450000000,
    "wall_step_count": 0
  }
}
```

Allowed `wall_sync_status` values:

- `synchronized`: an external host check supplied a bounded maximum error;
- `degraded`: a clock step, loss of synchronization, or bound violation was
  observed;
- `unknown`: no trustworthy bound was collected.

The capture loop must not execute chrony/NTP commands or parse host status.
Clock evidence is collected on the cold session lifecycle path. Missing host
evidence produces `unknown`, not a guessed healthy state.

Wall/monotonic start and end pairs allow offline detection of wall-clock steps.
They do not prove exchange-clock synchronization.

## Schema compatibility

This is a corpus schema v4 change, not an unversioned v3 optional tail.

- v3 remains loadable with existing exchange-time replay semantics.
- v4 loaders require capture-clock fields for rows whose manifest declares
  `origin=live` and captured-arrival capability.
- v3 rows expose capture clocks as unavailable; loaders must not zero-fill them
  and then treat zero as a real timestamp.
- a v4 file missing a manifest-required clock field is corrupt/incomplete, not
  silently downgraded to v3.
- an unsupported newer schema fails with `UnsupportedSchemaVersion`.
- support artifacts and caches never redefine clock truth.

Proposed loader representation:

```cpp
struct RecorderEventTimeV4 {
  std::int64_t exchangeTsNs{0};
  std::int64_t captureWallNs{0};
  std::int64_t captureMonoNs{0};
  std::int64_t captureSeq{0};
  std::int64_t ingestSeq{0};
  bool capturedArrivalAvailable{false};
};
```

The durable field remains `tsNs`; `exchangeTsNs` is an in-memory name that
makes algorithms choose an axis explicitly.

## Replay and validation rules

Recorder viewer/canonical replay may retain exchange-time buckets for v3 UI
compatibility. Captured-arrival replay is a separate explicit mode and orders
rows by:

```text
(captureMonoNs, ingestSeq)
```

It never sorts BBO and trades by `tsNs` to reconstruct their observed arrival
order.

Validation must report:

- non-positive or regressing `captureMonoNs` relative to `ingestSeq`;
- duplicate/non-increasing `ingestSeq`;
- non-increasing per-channel `captureSeq`;
- missing v4 live clock fields;
- wall-clock steps or a wall/mono delta mismatch;
- negative or impossible wall-minus-exchange samples;
- event identity availability by stream;
- imported/historical sessions that cannot support delivery replay.

Strict monotonic equality is allowed for `captureMonoNs`; `ingestSeq` resolves
it. A lower `captureMonoNs` at a higher `ingestSeq` is corruption or a capture
clock defect.

## Deterministic tests for the later implementation

1. Two streams with exchange timestamps in the opposite order preserve their
   capture order under `(captureMonoNs, ingestSeq)`.
2. Equal capture monotonic timestamps use `ingestSeq` as the only tie-breaker.
3. v3 loads without capture clocks and cannot silently select arrival replay.
4. v4 live row missing `captureWallNs` or `captureMonoNs` fails visibly.
5. Historical import declares no captured-arrival capability.
6. Negative `captureWallNs - tsNs` is retained and reported as clock evidence,
   not clamped.
7. Wall step changes `wall_sync_status` to degraded while monotonic ordering
   remains valid.
8. Writer backlog does not change the already captured stamp.
9. Event IDs survive capture, serialization, load, and replay for trade/BBO/depth
   where RuntimeV1 provides identity.
10. Trade and `aggTrade` remain different logical `feed_kind` values.

## Implementation boundary and evidence

- Recorder owns its rows, manifest, replay and validation contract.
- CXET owns RuntimeV1 payloads, stream metadata and exchange routing.
- A future true socket/frame receive timestamp would require a separately
  approved CXET public contract. This plan does not relabel capture time as that
  timestamp.
- No Git, build, test, generated rewrite, runtime capture, or live probe is
  implied by this document.
