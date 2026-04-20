# Viewer Baseline 2026-04

Purpose:

- Freeze the current practical viewer state after the replay/gui stabilization pass.
- Preserve a distinction between `architecturally improved` and `visually verified`.

What changed:

- selection rectangle exists via `Shift + LMB drag`
- selection summary overlay exists and is compact by design
- orderbook controls are now dollar-based, not percent-like
- orderbook rendering now prefers line-based readability over aggressive fill rendering
- fake orderbook tails before session start and after session end were removed
- top viewer controls persist between launches through `QSettings`
- viewer code is more decomposed and easier to audit:
  - controller logic is split by concern
  - scale widgets are isolated
  - chart surface and chrome are less entangled than before

Current top controls:

- `Trades Size`
  - trade dot radius scale
- `Full Bright @`
  - dollar notional level that reaches full orderbook line brightness
- `Min Visible`
  - dollar notional threshold below which book levels are not rendered

Current viewer role:

- baseline visual workbench for comparing original, transformed, and restored corpus outputs
- not just a market-data preview widget

Current live limitation:

- do not assume the baseline is visually correct end-to-end
- recent live screenshots still show scale/graph desynchronization:
  - chart shape reflects real session data
  - axes can remain frozen, zero-like, or visually detached from the plotted surface
- until that is fixed, the viewer is a strong internal baseline for structure and interaction, but not a fully trustworthy presentation baseline

Deliberate simplifications:

- compact overlay summary instead of a dense debug card
- line-based orderbook baseline instead of fill-heavy heatmap blocks
- `BookTicker` remains visible even when book filtering is strong

Next likely direction:

- keep this viewer stable after the live scale bug is removed
- build compression/reconstruction comparison surfaces on top of it
- add richer compare-only metrics without making the baseline view noisy
