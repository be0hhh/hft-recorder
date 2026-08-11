# hft-recorder - testing contract

## Testing stages

Testing is split into four practical layers.

### 1. Capture correctness

Must verify:
- session directory created correctly
- all required files exist
- manifest is valid JSON
- manifest/corpus schemas and capture contract are exactly current
- every live channel row has valid application-frame arrival clocks and identity
- historical rows are explicitly flagged and cannot enter live delivery
- arrival summary accounts exactly for every canonical row
- clock anomalies are retained as evidence, not silently normalized
- parser loss with either local clock unavailable is sticky for the capture
  epoch and represented by a `0/0` gap intersecting every receive interval for
  that source/channel
- `Stop` waits for a whole-frame capture lease and cannot split a multi-event
  frame or committed depth transaction across the sealed boundary
- channel JSON lines follow the manifest-declared current schema
- depth tape and sidecar are paired exactly
- snapshot cadence works

### 2. Corpus loading

Must verify:
- manifest-first loading works
- old manifests, fallback filenames and flat depth are rejected
- all channel files parse
- captured-arrival ordering and identity tie-breaks are preserved
- identifiable publisher rejection and an unrepresentable committed depth
  transaction produce a loss-ledger gap rather than a falsely exact corpus
- integer values survive exactly
- malformed input reports deterministic errors
- stale seek indexes are ignored deterministically
- replay and loader can share the same corpus fixtures and reach the same
  structural verdict
- binary corpus ABI/schema/CRC/index/source directory are exact
- any selected capture/source gap fails closed
- any selected recorded-only record fails trader replay, even on an optional
  channel
- unavailable required strategy channels fail closed

Fixture substrate:
- corpus fixtures live under `tests/fixtures/session_corpus/`
- fixture directories document intended status in `README.md`
- new loader/replay structural tests should prefer these fixtures over ad hoc
  temp directories

### 3. Validation correctness

Must verify:
- original normalized corpus and decoded pipeline output match exactly for
  lossless pipelines
- mismatches report channel and event index
- backtest strategy visibility occurs only at captured delivery
- pending execution, user-data and timer work cannot advance past an earlier
  undispatched captured delivery; exact ties use venue, delivery, internal-work
  order
- venue execution ordering remains separate and is never scheduled after
  delivery
- no synthetic market-data latency is added; order/cancel/user-data latency
  remains an execution setting

### 4. Lab metrics

Must verify:
- every pipeline reports ratio and speed fields
- ranking is deterministic on the same inputs

## GUI acceptance

The GUI acceptance path is:
1. create a session
2. stop the session cleanly
3. open it in the Sessions page
4. open raw charts in Validation
5. run baselines in Lab
6. see dashboard results

## Current rule

The old large `.cxrec`-oriented unit/integration matrix is historical design
material, not the immediate test gate for the GUI-first product.

The immediate test gate is:
- application-frame clocks and loss accounting are correct
- only exact current corpus loads for backtest
- lossless pipelines validate correctly
- GUI views can consume the results
