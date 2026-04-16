# hft-recorder - implementation notes

## Current scope

The project is not just building a recorder anymore.

The project is building:
- a specialized C++ compression library for market data
- a Python lab for idea generation
- a benchmark/capture system that proves which custom ideas are best

## Implementation priorities

### 1. Custom ideas must be numerous

The implementation plan must deliberately explore many custom candidates.
Do not converge too early on one transform or one codec family.

### 2. Standard codecs remain mandatory

They are the baseline and proof layer.
Every serious custom candidate must be measured against them.

### 3. Three stream families are first-class

The first serious phase must already include:
- trades / aggTrade
- L1 / bookTicker
- orderbook updates

Do not postpone orderbook entirely out of the first real design phase.

### 4. Per-stream winners are allowed

The implementation must assume that:
- trades may prefer one pipeline
- L1 may prefer another
- orderbook may require a third

That is a valid and expected outcome.

### 5. C++ core is final, Python is exploratory

Use Python for:
- fast prototyping
- data analysis
- early transform experiments

Move winning ideas into the C++ core.

### 6. Online compression is a hard requirement

The end state must support:
- event arrives
- transform immediately
- compress immediately or by small rolling block
- write compressed bytes directly

If a method is only good offline, it can still matter for archive mode, but it cannot automatically win the live path.

## Immediate documentation gaps to fill

The docs must explicitly define:
- custom idea catalog
- dataset capture protocol
- comparison matrix
- orderbook representation experiments

Without those documents, implementation will drift too quickly.
