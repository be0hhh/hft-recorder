# hft-recorder - source layout and variants

## Top-level code layout

```text
include/hftrec/
src/gui/
src/core/
src/support/
src/variants/
tests/
bench/
scripts/python_lab/
```

## `src/gui/`

This is the Qt 6 QML application layer.

Subdirectories:
- `app/`
- `qml/`
- `models/`
- `viewmodels/`

## `src/core/`

Stable backend implementation.

Current important subdirectories:
- `capture/`
- `corpus/`
- `validation/`
- `lab/`
- `cxet_bridge/`
- `common/`
- `metrics/`

Old scaffold-era subdirectories such as `block/` and `codec/` may remain, but
they are no longer the center of the product architecture.

## `src/variants/`

Experimental custom compression forest.

Families:
- `trade/`
- `l1/`
- `orderbook/`

These candidates consume the canonical corpus or a canonical normalized binary
derivation of it. They do not replace the corpus.

## Variant naming

Use:
- `varNN_..._cpp`
- `varNN_..._pyproto`

Examples:
- `var01_raw_zstd_cpp`
- `var02_trade_delta_pack_cpp`
- `var03_l1_spread_anchor_cpp`
- `var04_orderbook_keyframe_delta_cpp`

## Promotion rule

Do not promote experimental code into `src/core/` too early.

Promote only when:
- multiple variants need the same helper
- the helper is stable
- the shared abstraction does not make hot paths worse
