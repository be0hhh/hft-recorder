# hft-recorder / CXETCPP triangle base architecture audit

## Scope

This document freezes the current base architecture for the future triangle:

- CXETCPP is the only algo-facing exchange / execution API
- hft-recorder is the corpus host, viewer, compression lab, local venue, and future orchestration shell
- future algo runtime must speak only to CXETCPP, never to recorder-native DTOs or storage details

This is not a feature roadmap. This is the current architecture gate and gap audit.

## Frozen decisions

### Macro shape

The base style is a modular monolith with four internal domains:

1. Storage / Corpus
2. Market Data
3. Execution
4. Presentation / Orchestration

Allowed dependency direction:

- Presentation / Orchestration may depend only on stable domain contracts and read-model providers
- Market Data may depend on Storage / Corpus contracts
- Execution may depend on Storage / Corpus contracts and execution contracts
- Storage / Corpus depends on no higher domain
- no core domain may depend on Qt / QML types

### Truth model

The system truth is corpus-first.

Required invariants:

- live and history must share one row / event family
- live and history must share one ordering model
- hot memory is a front-cache over corpus semantics, not a separate semantic model
- JSON is the first durable backend implementation, not the architectural truth

### Local venue contract

The local execution path is split into two layers:

- wire v1: algo sends sendWs().object(order).exchange(hftrecorder_local) through CXETCPP and receives transport-level OrderAck
- internal semantic contract: recorder owns normalized execution events and reserves additive growth for ack, reject, state change, fill, account change, and position change

The wire contract may stay minimal. The internal execution family must not be constrained by that minimal wire contract.

## Stable domain contracts

### Storage / Corpus

Current stable or near-stable seams:

- storage::IEventSink
- storage::IEventSource
- storage::IHotEventCache
- storage::IStorageBackend
- storage::CompositeEventSink
- SessionManifest
- CorpusLoader

Rules:

- corpus/session readers consume manifest-declared artifacts, not hardcoded filenames as product truth
- durable backends must satisfy append/read/materialize/session semantics independent of backend format
- consumer semantics must not depend on JSON line tailing forever

### Market Data

Current stable or near-stable seams:

- market_data::IMarketDataIngress
- recorder-owned normalized row family in core/replay/EventRows.hpp

Rules:

- CXET callback payloads are bridge input only
- recorder-owned rows are the semantic contract for capture, viewer, replay, validation, and future backtest
- one identity model only: exchange + market + symbol + sourceId

### Execution

Current stable or near-stable seams:

- execution::IExecutionVenue
- execution::IExecutionEventSink
- execution::IExecutionEventSource
- execution::LiveExecutionStore

Rules:

- execution events are recorder-owned
- transport socket frames do not leak into higher layers as the semantic contract
- LocalOrderEngine is an implementation detail behind execution seams, not the future architecture limit

### Presentation / Orchestration

Current stable or near-stable seams:

- ILiveDataProvider
- viewer read/materialization providers
- Qt viewmodels/models/controllers as app-shell adapters

Rules:

- presentation reads only through provider/read-model seams
- presentation must not own storage policy, execution policy, or session truth
- presentation must not treat JSON layout as product truth

## Current reality audit

### Severity A: architecture risks that must stay visible

1. LocalOrderEngine lifecycle is still hidden behind globalLocalOrderEngine().
   Current usage spans capture runtime and local exchange server, so ownership is not yet composition-root clean.

2. Viewer live-source ownership is still hidden behind LiveDataRegistry::instance().
   This is a temporary service locator in the presentation path.

3. JsonTailLiveDataProvider still encodes current file-layout truth.
   It tails trades.jsonl, bookticker.jsonl, depth.jsonl, discovers snapshot_*.json, and keeps its own full history vectors.

4. SessionReplay remains file-oriented and JSON-oriented.
   It is a working replay implementation, but not yet a backend-neutral materialization contract.

### Severity B: hardcoded backend assumptions

1. Current viewer live mode distinguishes between:
   - in-memory registry-backed live sources
   - JSON-tail session directories

   This is useful now, but it proves the consumer seam is still backend-shaped.

2. Session artifact names still appear as direct control flow in viewer/replay code:
   - trades.jsonl
   - bookticker.jsonl
   - depth.jsonl
   - snapshot_*.json

3. Current hot/live provider path can accumulate exact full history in RAM, which does not scale as the long-session contract.

### Severity C: temporary but acceptable for this phase

1. LiveEventStore is an exact in-memory cache and read source.
   This is acceptable as the hot cache baseline while large-session materialization remains future work.

2. JsonSessionSink is the first durable backend.
   This is acceptable as long as higher layers keep referring to backend contracts, not to JSON as permanent truth.

## Memory ownership map

Current memory roles:

- storage::LiveEventStore: hot exact cache, session-local, append/read in RAM
- JsonSessionSink: durable append path, JSON backend only
- SessionReplay: historical materialization and exact replay state, currently full-load oriented
- JsonTailLiveDataProvider: temporary warm/history duplication for UI polling
- chart/viewer cache: advisory visible-window cache only

Classification:

- hot: LiveEventStore
- warm: live provider visible window and last batch
- historical: SessionReplay loaded rows, provider history vectors
- advisory: viewport/render caches

Rules to keep:

- exact long-session history must not be duplicated indefinitely across hot cache, provider cache, and viewer cache
- visible-window caches are allowed to be advisory only
- future history materialization must move toward on-demand backend-backed reads

## External algo-facing contract

Strict future statement:

- algo talks only to CXETCPP
- recorder is allowed to appear only as a venue behind a CXETCPP exchange id such as hftrecorder_local
- algo must not depend on:
  - recorder-native DTOs
  - session folder layout
  - JSON filenames
  - viewer/provider implementation details

What stays in CXETCPP:

- request builders and public exchange/execution API
- local venue exchange id and transport envelope

What stays in hft-recorder:

- corpus/session contracts
- recorder-owned market-data rows
- recorder-owned execution events
- viewer/read-model/materialization logic
- local venue implementation

## Acceptance criteria

### Dependency gate

- no Qt/QML types cross into core domain contracts
- presentation depends on contracts/providers, not on storage layout truth
- higher domains do not own lower-domain singletons as the final architecture

### Semantic gate

- live, session history, and replay keep one row/event family
- ordering semantics remain based on tsNs plus recorder ingest ordering metadata
- source identity stays deterministic across multi-source live and historical modes

### Storage gate

- adding a second backend must not require consumer contract rewrites
- JSON filenames and directory layout are implementation details, not semantic truth

### Execution gate

- OrderAck remains wire-only
- recorder internal execution family remains additive for future fills/account/position growth

## Implementation sequence after this audit

1. Remove hidden ownership from temporary globals by moving registry/venue ownership into the app shell.
2. Freeze the stable event families explicitly for market data, execution, and diagnostics.
3. Introduce backend-neutral session/materialization contracts above JSON file traversal.
4. Bound memory duplication between hot cache, provider cache, and viewer cache.
5. Keep presentation on read-model seams only.

## Immediate repository guidance

When changing hft-recorder now:

- prefer contract-first changes in core/storage, core/market_data, core/execution, and provider seams
- do not add new JSON-path assumptions into viewer logic unless they are explicitly temporary
- do not let current local-venue ack-only transport shape the future execution domain
- treat current globals as temporary compatibility seams, not as architecture endorsement
