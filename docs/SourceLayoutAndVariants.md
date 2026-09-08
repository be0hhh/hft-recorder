# hft-recorder - source layout and variants

## Top-level code layout

```text
src/
  core/
    CMakeLists.txt
    include/hftrec/
    src/
  app/
    CMakeLists.txt
    src/
  gui/
    CMakeLists.txt
    src/
  support/
    CMakeLists.txt
    src/
  variants/<family>/<variant>/
    CMakeLists.txt
    src/
  corpus_contract/
    CMakeLists.txt
    include/hftrec/
tests/
docs/
scripts/PythonLab/
```

Component `CMakeLists.txt` files remain at their component roots. Public headers
and private implementation roots stay separate.

## `src/Gui/src/`

This is the Qt 6 QML application layer.

Subdirectories:
- `app/`
- `qml/`
- `models/`
- `viewmodels/`

## `src/Runtime/src/`

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

## `src/Lab/Variants/`

Experimental custom compression forest.

Families:
- `trade/`
- `l1/`
- `orderbook/`

These candidates consume the canonical corpus or a canonical normalized binary
derivation of it. Each variant keeps implementation files in its own `src/`
subdirectory. They do not replace the corpus.

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

Do not promote experimental code into `src/Runtime/src/` too early.

Promote only when:
- multiple variants need the same helper
- the helper is stable
- the shared abstraction does not make hot paths worse
