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

---

## Recorder thread model (capture mode, MVP)

The recorder binary (`hft-recorder`) launches **seven** pinned threads. Each
has one well-defined job and does not share mutable state with any other
thread except via an SPSC ring or lock-free queue owned by the writer.

```
                          ┌─────────────────────────────────────────────┐
                          │                  main()                     │
                          │  cxet::initBuildDispatch();                 │
                          │  load .env; bind CPU affinities;            │
                          │  spawn 6 threads below; join on SIGTERM.    │
                          └─────────────────────────────────────────────┘
                                             │
      ┌─────────────────────┬────────────────┼─────────────────────┬──────────────────────┐
      ▼                     ▼                ▼                     ▼                      ▼
┌───────────┐        ┌───────────┐     ┌───────────┐        ┌───────────┐         ┌─────────────┐
│ producer  │        │ producer  │     │ producer  │        │ producer  │         │   control   │
│ trades    │        │ bookTick  │     │ depth@0ms │        │ snapshot  │         │    SIGTERM  │
│ CPU 2     │        │ CPU 3     │     │ CPU 4     │        │ CPU 5     │         │    watchdog │
├───────────┤        ├───────────┤     ├───────────┤        ├───────────┤         │    metrics  │
│CxetStream │        │CxetStream │     │runSubscr. │        │runGetOB    │        │    CPU 7    │
│<TradePub> │        │<BookTick> │     │OrderBook  │        │ByConfig    │        └─────────────┘
│  loop     │        │  loop     │     │Delta cb   │        │+ sleep 60s │
└─────┬─────┘        └─────┬─────┘     └─────┬─────┘        └─────┬─────┘
      │ SPSC-1        SPSC-2 │                │ SPSC-3         SPSC-4 │
      ▼                     ▼                 ▼                     ▼
┌───────────┐        ┌───────────┐     ┌───────────┐        ┌───────────┐
│ writer    │        │ writer    │     │ writer    │        │ writer    │
│ trades    │        │ bookTick  │     │ depth@0ms │        │ snapshot  │
│ CPU 8     │        │ CPU 9     │     │ CPU 10    │        │ CPU 11    │
├───────────┤        ├───────────┤     ├───────────┤        ├───────────┤
│ delta+    │        │ delta+    │     │ delta+    │        │ delta+    │
│ VARINT    │        │ VARINT    │     │ VARINT    │        │ VARINT    │
│ → pwrite  │        │ → pwrite  │     │ → pwrite  │        │ → pwrite  │
│ → fsync   │        │ → fsync   │     │ → fsync   │        │ → fsync   │
└───────────┘        └───────────┘     └───────────┘        └───────────┘
  trades.cxrec       bookticker.cxrec   depth0ms.cxrec       snapshot.cxrec
```

Thread roles:

| Thread | CPU pin (suggested) | Responsibility |
|--------|---------------------|----------------|
| `main` | CPU 0 | Init, env, spawn, signal wait, join. |
| `producer:trades` | CPU 2 | Owns a `CxetStream<TradePublic>`; drains its SPSC ring and pushes each event into `SPSC-1`. |
| `producer:bookTicker` | CPU 3 | Same shape, owns `CxetStream<BookTickerData>` → `SPSC-2`. |
| `producer:depth0ms` | CPU 4 | Calls `runSubscribeOrderBookDeltaByConfig(cfg, cb)`; `cb` pushes `OrderBookSnapshot` onto `SPSC-3`. Thread is blocked inside the library's run loop. |
| `producer:snapshot` | CPU 5 | Sleeps 60 s, calls `runGetOrderBookByConfig(cfg, out)`, pushes `out` onto `SPSC-4`. |
| `writer:<stream>` × 4 | CPU 8–11 | Each owns one `.cxrec` file, its own delta encoder state, and a VARINT encoder. Pops from its SPSC, applies block-flush policy (see FILE_FORMAT.md), `pwrite` + periodic `fsync`. |
| `control` | CPU 7 | Watchdog (heartbeats), metrics push to Prometheus, graceful shutdown on `SIGTERM`/`SIGINT`. |

Why 4 writer threads instead of one multiplexed writer: a slow `fsync` on one
file must not backpressure another stream's capture. Each file's lifecycle
is independent; a dropped `depth@0ms` event is already a gap that must be
handled separately from the other streams anyway.

Why producers are separate from writers: the CXETCPP `run…` call is a
blocking IO loop that should not do compression work on the same thread as
WS decode. Moving encode+pwrite into dedicated writer threads keeps the IO
thread latency-bounded and lets us pin them on dedicated cores.

CPU pinning: via `cxet::osThreadAffinity` (or whatever public pinning helper
the library exposes; otherwise via `sched_setaffinity` in a tiny `os/` helper
local to the app).

### Bench-mode thread model (analysis mode)

The offline `hft-recorder-bench` binary is single-threaded by design — it
replays one captured file through one codec, measures per-block RDTSC, and
pushes metrics to Prometheus. This keeps the measurement noise floor low
(no cross-core cache traffic). Multiple codec passes are run sequentially
inside a driver script, not in parallel threads.
