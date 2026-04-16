# hft-recorder - architecture

## Main architecture goal

The project should be structured around a **custom compression library core** with supporting research infrastructure.

The architecture must reflect three roles:
- `C++ core library`
- `Python research lab`
- `benchmark and recorder harness`

## Layer 1. C++ core library

This is the main deliverable.

Responsibilities:
- implement winning custom representations
- implement winning custom transforms
- implement winning custom codecs or hybrid pipelines
- provide stream-family-specific compression paths for:
  - trades / aggTrade
  - L1 / bookTicker
  - orderbook updates

This core should not be designed as a single flat "one codec for all" engine.
It should allow different strategies per stream family.

## Layer 2. Python research lab

This layer exists for fast iteration.

Responsibilities:
- prototype transforms quickly
- test many ideas cheaply
- compare standard codecs
- produce plots, tables, and exploratory analysis

This layer may contain implementations that are never promoted to the C++ core.

## Layer 3. Recorder and benchmark harness

This layer connects the core library to reality.

Responsibilities:
- capture live normalized events from `CXETCPP`
- run offline comparisons on captured corpora
- run online compression experiments
- produce reproducible results

## Two operating modes

### Capture mode

Purpose:
- consume live `CXETCPP` streams
- apply a selected representation and compression path
- write compressed output directly during recording

### Analysis mode

Purpose:
- load captured datasets
- replay them through many strategies
- compare standard, Python, and custom paths

## Stream-family specialization

The architecture must explicitly allow separate subsystems for:

### Trades / aggTrade
- event-centric pipelines
- compact row and column layouts
- delta-heavy transforms

### L1 / bookTicker
- bid/ask pair transforms
- spread-relative or mid-relative transforms
- very cheap online encoding

### Orderbook
- representation search first
- compression second
- reconstruction-aware decoding

Orderbook should be treated as its own sub-architecture, not as "just another event stream".

## Important rule

Do not design the repo as if the benchmark harness were the main artifact.

The benchmark harness proves the value of the custom library.
The custom library is the center.
