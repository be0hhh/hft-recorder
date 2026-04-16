# hft-recorder - orderbook representation experiments

## Why orderbook is separate

Orderbook is not just another event stream.
For orderbook, the biggest decision may be representation rather than codec.

## Main question

What exact object should the specialized library compress for orderbook data?

Possible answers:
- raw exchange-like update event
- normalized changed-level list
- top-N projection
- periodic keyframe with incremental deltas
- anchor-relative internal representation

## Representation families to test

### 1. Raw update form
- simplest baseline
- minimal preprocessing
- probably worst compression input

### 2. Changed-level only
- store only touched levels
- split by side
- encode level count explicitly

### 3. Top-N form
- keep only first N levels
- useful if replay target is feature generation rather than exact book reconstruction

### 4. Keyframe + delta chain
- periodic full state
- in-between compact deltas
- better for restart and gap handling

### 5. Anchor-relative form
- encode prices relative to:
  - best bid
  - best ask
  - midprice
  - previous touched level

## Evaluation criteria

For each representation, measure:
- bytes before final codec
- bytes after codec
- encode complexity
- decode complexity
- ability to rebuild a usable local book
- behavior under sequence gaps

## Expected outcome

This document should eventually lead to:
- one preferred archive-oriented orderbook representation
- one preferred replay-oriented orderbook representation

They may be the same, but they do not have to be.
