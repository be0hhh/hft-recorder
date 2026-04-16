# hft-recorder - research program

## Central thesis

The coursework should result in a **specialized market-data compression library**.

The benchmark and recorder parts exist to support that goal, not replace it.

The custom library must be justified by evidence:
- standard baselines
- Python experiments
- custom C++ pipelines
- live-path validation

## Research tracks

### Track A. Dataset capture

Capture real live datasets from `CXETCPP` for all three domains:
- trade-like stream (`aggTrade` where that is the actual semantics)
- `bookTicker` / `L1`
- orderbook updates

Target:
- at least 20 minutes per stream family
- preferably more than one market regime

### Track B. Custom idea generation

Build a large catalog of custom ideas.

The project should not stop at one or two obvious transforms.
It should explore:
- stream-specific transforms
- layout-specific transforms
- block-level tricks
- orderbook-specific representations
- online-friendly and offline-heavy variants

### Track C. Baseline comparison

Compare every serious candidate against standard codecs and simple hybrid baselines.

### Track D. Python laboratory

Use Python to test ideas cheaply before committing to C++ implementations.

Python is part of the official research methodology, not an afterthought.

### Track E. C++ core library

Promising custom winners must be moved into the actual C++ compression core.

This is the main technical deliverable of the coursework.

### Track F. Online validation

The strongest candidates must be tested in the online path:
- event arrives
- transform happens immediately
- compression happens immediately or by rolling block
- data is written already compressed

## Success criteria

The phase is successful if it produces:
- a documented custom idea catalog
- captured datasets for trades, L1, and orderbook
- comparison results against standard baselines
- per-stream winners
- one documented C++ core direction for the specialized library
- a justified online recording strategy

## Non-goals

- forcing one universal winner for all streams
- rewriting `CXETCPP`
- pretending a standard codec is enough without measuring custom alternatives
- freezing the final file format too early
