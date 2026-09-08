# Market-making simulation notes

This note records two external references that are useful for future work on
`hft-recorder` once the canonical corpus and viewer are stable:

- sebjai, `short_term_alpha.ipynb`, a notebook implementation of the Cartea,
  Jaimungal, and Penalva short-term-alpha market-making model:
  <https://gist.github.com/sebjai/c6ff3850dea37d28d3fa7d3aef59722b>
- Lalor and Swishchuk, "Market Simulation under Adverse Selection",
  arXiv:2409.12721v1, September 19, 2024:
  <https://arxiv.org/html/2409.12721v1>

These sources are not compression algorithms. They are useful because they
define a realistic downstream consumer of the corpus: a market-making simulator
that needs exact L1/orderbook replay, plausible fill modelling, and explicit
adverse-fill metrics.

## Why this matters here

The current project priority is still:

1. capture canonical market data,
2. replay and validate it,
3. visualize it,
4. run compression experiments over the same corpus.

The market-making material gives a concrete reason to keep the corpus richer
than a trade-only tape. A strategy simulator needs:

- `bookTicker` best bid/ask continuity,
- orderbook depth near the touch,
- trade/aggTrade semantics kept honest,
- time-aligned replay across streams,
- enough size information to reason about fill probability.

That aligns with the current GUI and corpus direction. It does not replace the
compression lab, but it can become a validation workload for reconstructed data:
if a codec changes best-bid/best-ask timing, queue sizes, or near-touch depth,
the simulator's fills and adverse-fill counts will change.

## What the references contribute

### Short-term alpha notebook

The notebook is an implementation sketch for the optimal posting model from
Cartea, Jaimungal, and Penalva. The important concepts for this repo are:

- state variables: inventory, time, and short-term alpha,
- controls: whether to post at best bid and/or best ask,
- output surface: posting decisions over the discrete state grid,
- implementation direction: a Python lab can solve or approximate the decision
  surface before anything is moved into C++.

This is a Python-research candidate, not an immediate GUI feature.

### Adverse-selection paper

The paper's most useful contribution is the simulation critique:

- naive simulations often assume unrealistic fills,
- fill probability should depend on queue/depth conditions,
- adverse fills should be tracked separately from non-adverse fills,
- ignoring adverse fills can materially inflate apparent strategy performance.

Their adverse-fill definition is directly testable on our replay model:

- bid-side fill is adverse if the next best bid move is lower before any
  favorable mark-to-market move,
- ask-side fill is adverse if the next best ask move is higher before any
  favorable mark-to-market move.

This suggests an eventual `core/sim/` or `core/lab/market_making/` module that
uses `SessionReplay` and the canonical corpus to compute fill diagnostics.

## Data requirements for hft-recorder

To support this later without repainting the architecture:

- preserve exact `bookTicker` rows and timestamps,
- preserve depth deltas and snapshots with sequence/integrity checks,
- keep `trades` and `aggTrade` labels separate,
- retain quantities as scaled integers,
- expose replayed best bid/ask and near-touch depth to lab code,
- avoid compression transforms that lose event ordering or price-level identity.

For Binance FAPI, this remains a limitation: public trade-like data may be
`aggTrade`, not raw individual trades. That is acceptable for compression and
viewer work if labelled honestly, but it weakens any fill-probability claim.

## Possible lab experiments

### Experiment 1: adverse-fill counter

Given a hypothetical passive fill event:

- side: bid or ask,
- fill price,
- fill timestamp,
- order size,

scan future `bookTicker`/book states until the first relevant best-price move.
Classify the fill as adverse, non-adverse, or unresolved.

This can be implemented without solving the full optimal-control problem.

### Experiment 2: near-touch queue proxy

For each candidate posting point:

- record best bid/ask,
- record visible size at that level,
- record recent marketable trade volume,
- estimate whether an order of size `q` could plausibly fill.

This is only a proxy without true queue position, but it is better than assuming
all posted orders fill.

### Experiment 3: compression sensitivity check

Run the same adverse-fill counter on:

- original corpus,
- decoded output from a lossless codec,
- any future lossy/downsampled research representation.

Lossless codecs should match exactly. Any lossy representation must report how
often it changes:

- fill classification,
- best-bid/best-ask event time,
- spread,
- near-touch depth.

## What not to do yet

- Do not add a live trading or strategy-execution feature.
- Do not claim real queue-position simulation from Binance public data.
- Do not mix this with the compression ranking score until the base corpus and
  validation path are mature.
- Do not optimize the viewer around this model; the viewer remains a replay and
  validation surface first.

## Practical next step

After the canonical corpus is stable, add a small offline lab utility:

```text
hft-recorder-lab adverse-fills <session-dir> --side bid|ask --size-e8 N
```

The first version should output only counts and CSV rows:

- candidate timestamp,
- side,
- price,
- size,
- next best-price move timestamp,
- classification,
- mark-to-market delta.

That would give this project a credible bridge from "we can replay the book" to
"we can evaluate whether reconstruction quality matters for a real HFT-style
workload."
