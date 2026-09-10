# Delivery timestamps: current contract

Status: implemented source contract, static-only evidence.

The canonical design is documented in
[`BacktestEngineContract.md`](BacktestEngineContract.md) and the durable JSON
layout is documented in [`SESSION_CORPUS_FORMAT.md`](../docs/SESSION_CORPUS_FORMAT.md).
This file records the hard cut from the earlier recorder-callback proposal.

## Clock ownership

Live arrival is sampled by `hft-parser` at
`hft-parser.application-frame-ready`, after the complete application frame or
committed depth transaction is accepted. The two adjacent observations are:

- `CLOCK_REALTIME`, for comparison with exchange/source epoch time;
- `CLOCK_MONOTONIC`, for deterministic within-session delivery ordering.

The recorder never replaces either value with disk-write, queue-drain or
callback time. It persists the parser observation together with producer,
source-generation, session, frame, shard and event-ordinal identity.

All records projected from one accepted application frame share one clock
pair. All chunks and logical parts of one committed depth transaction also
share one pair; replay applies the complete transaction before invoking the
strategy.

The parser holds one capture lease across that whole frame/transaction. A
recorder `Stop` first closes admission and then waits for every acquired lease,
so the sealed final drain cannot contain only a prefix of an application
frame.

## Timestamp meanings

- `exchangeTimestampNs`/`tsNs` is exchange-owned event time. Zero means the
  exchange timestamp is unavailable and must be marked explicitly.
- `receiveRealtimeNs` is local epoch time at the parser application boundary.
- `receiveMonotonicNs` is the local monotonic coordinate at the same boundary.
- `shardSequence` plus frame/source identity resolves equal clock samples; it
  is an ordering identity, not a clock.

Negative realtime-minus-exchange observations, realtime regressions,
non-increasing monotonic samples and missing exchange timestamps are preserved
as evidence. They are not clamped, fabricated or rejected by default.

If either local clock cannot be sampled, the affected source/channel is written
to the loss ledger with the unknown receive range `0/0`. That range intersects
every selected receive interval and therefore prevents an exact backtest.

Ring overflow, any identifiable publisher rejection and a committed depth
transaction that cannot be represented by the capture schema are also explicit
source/channel gaps. Capture must never silently omit one of those events and
still produce an exact-eligible interval.

## Corpus and replay cut

There is one current writer and one current reader contract:

- sealed parser binary corpus for direct source/time selection;
- current recorder JSON session with captured-arrival rows and paired depth
  tape/sidecar files.

Old recorder sessions, flat `depth.jsonl`, recorder-callback arrival stamps and
exchange-time-only replay are unsupported. There is no migration reader or
silent downgrade.

Backtest uses captured monotonic delivery for strategy visibility and a
separate exchange-projected venue plane. Synthetic market-data delivery
latency is removed; order, cancel and user-data execution latency remains an
independent simulation model.

## Current channel evidence

The binary protocol reserves bookticker, trade, depth, liquidation, mark price,
index price, funding and price-limit payloads. A reserved payload is not proof
of a live producer. The currently connected parser publication paths are
bookticker, trade and committed depth transactions. A strategy requiring any
other unavailable channel fails closed during corpus selection.

An unchanged retained BBO is still captured with its newer arrival pair and
the same state sequence. A consumer that already installed that sequence uses
the row only to refresh liveness; a selection that begins at that row installs
its complete payload as the initial state. The flag may never conceal changed
prices, quantities or side-presence bits.

Build, unit-test, runtime-capture and live-exchange proof are separate gates and
are not implied by this static contract.
