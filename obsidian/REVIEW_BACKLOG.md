# Review Backlog

Purpose:

- Turn current review findings into an implementation-ready queue.
- Give the urgent chat a clean starting backlog.

Verified at:

- `2026-04-19`

Priority queue:

1. `Critical` capture contract honesty
   - owner: `capture/gui/cli`
   - status: `queued`
   - verified_against: `src/core/capture/CaptureCoordinator.cpp`, `src/gui/viewmodels/CaptureViewModel.cpp`, `src/app/capture_cli.cpp`
   - problem: UI/docs/config look generic, runtime binding is effectively Binance FAPI and one symbol per coordinator
   - expected behavior: product wording and runtime scope match; current limits are explicit
   - acceptance: no user-facing path implies unsupported exchange/market behavior

2. `High` orderbook sequence and gap validation
   - owner: `replay`
   - status: `queued`
   - verified_against: `src/core/replay/BookState.cpp`, `src/core/replay/SessionReplay.cpp`
   - problem: replay applies deltas without explicit continuity checks
   - expected behavior: gap or invalid update-id chain is detected and surfaced
   - acceptance: replay cannot silently present a desynced book as healthy truth

3. `High` replay parser hardening
   - owner: `replay`
   - status: `queued`
   - verified_against: `src/core/replay/JsonLineParser.cpp`
   - problem: handwritten parser is a fragile seam at the canonical corpus boundary
   - expected behavior: canonical files are parsed by a robust path with deterministic failure behavior
   - acceptance: malformed JSON, escaping, and missing-key cases are covered by tests

4. `High` manifest and serializer escaping
   - owner: `capture`
   - status: `queued`
   - verified_against: `src/core/capture/SessionManifest.cpp`, `src/core/capture/JsonSerializers.cpp`
   - problem: direct string concatenation can emit invalid JSON
   - expected behavior: paths, ids, warnings, and errors serialize safely
   - acceptance: quoted paths and error strings keep corpus JSON valid

5. `Medium` CXET boundary cleanup
   - owner: `capture/cxet bridge`
   - status: `queued`
   - verified_against: `src/core/capture/CaptureCoordinator.cpp`, `src/core/cxet_bridge/CxetCaptureBridge.cpp`
   - problem: intended seam exists but real CXET calls bypass it
   - expected behavior: recorder logic can be reasoned about and tested without smearing runtime details everywhere

6. `Medium` viewer envelope and scale audit
   - owner: `viewer/replay`
   - status: `queued`
   - verified_against: `src/gui/viewer/ChartController.cpp`
   - problem: initial viewport uses final state more than full historical envelope
   - expected behavior: initial visible range reflects session history, not only terminal book state

7. `Medium` failure propagation audit
   - owner: `capture/gui`
   - status: `queued`
   - verified_against: `src/core/capture/CaptureCoordinator.cpp`, `src/gui/viewmodels/CaptureViewModel.cpp`
   - problem: background failures and partial batch failures are not yet a crisp contract
   - expected behavior: GUI/CLI/manifest all report partial failures deterministically

Non-priority for the first cycle:

- full Grafana integration
- block/codec resurrection
- deep variant optimization
- placeholder CLI expansion
