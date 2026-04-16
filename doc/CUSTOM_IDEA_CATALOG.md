# hft-recorder - custom idea catalog

## Purpose

This document is the central catalog of custom compression ideas.

The project should deliberately explore many candidates and avoid converging too early.
The goal is to produce a specialized library, so breadth of ideas matters.

## Trade / aggTrade ideas

Candidate families:
- delta by event id
- delta by timestamp
- delta by price
- delta by qty
- side-bit packing
- signed vs unsigned split paths
- row layout vs column layout
- block-local price anchor
- block-local qty dictionary
- repeating-side run encoding
- micro-batching with fixed event count windows
- split hot fields and cold fields into separate substreams
- encode price movement direction separately from movement magnitude
- small-tick optimized bit packing

## L1 / bookTicker ideas

Treat `bookTicker` as `L1 orderbook`.

Candidate families:
- delta from previous bid/ask
- encode bid and ask relative to previous mid
- encode spread separately
- change-mask for which fields changed
- symmetric packing for bid/ask pair
- quantity-specific low-entropy path
- alternating small/large move classes
- rolling anchor every N events
- block-local dictionary for repetitive spread states
- event coalescing by tiny windows

## Orderbook update ideas

Candidate families:
- raw changed-level list
- changed-level list with side split
- top-N only projection
- periodic keyframe + delta chain
- anchor-relative price indexes
- store only touched levels plus level count
- sparse level bitmap plus payload
- side-separated columnar blocks
- absolute price for keyframe, relative for deltas
- event-local sorting normalization
- grouped levels by distance to best bid/ask
- reconstruction-first representation for faster replay

## Cross-stream ideas

Ideas that may apply to more than one family:
- delta + varint
- delta + standard codec
- columnar blocks + standard codec
- bit packing before generic compression
- block-local dictionary
- adaptive codec switching by block type
- cheap online path and heavy offline path

## Candidate status labels

Every idea should later be tagged with one of:
- `not tested`
- `python prototype`
- `c++ prototype`
- `baseline compared`
- `online validated`
- `rejected`
- `winner`

## Important rule

Do not delete ideas only because they seem strange early on.
First collect them, then rank them through measurement.
