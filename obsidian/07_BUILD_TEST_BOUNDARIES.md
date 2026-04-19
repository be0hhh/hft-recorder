# Build Test Boundaries

Owns:

- Module and target boundaries.
- What links to what.
- What is stable core vs optional UI/bench/test surface.

Primary targets:

- root `CMakeLists.txt` imports `src/` and `tests/`
- `hftrec_core` is the main backend static library
- `hftrec_support` wraps external codecs and links to `hftrec_core`
- `hft-recorder` is the CLI executable
- `hft-recorder-bench` is the bench executable
- `hft-recorder-gui` is the Qt GUI executable when Qt6 is found

Boundary rules that matter:

- `hft-recorder` consumes prebuilt `CXETCPP` through `cxet_lib`
- it must not build parent-project internals with `add_subdirectory(..)`
- GUI links `hftrec_core` and `hftrec_support`
- CLI/bench also link selected variant targets

Tests:

- `tests/unit/` covers smoke, serializers, replay, parser, and book state basics
- `tests/integration/` exists as target boundary
- `tests/bench/` exists as opt-in benchmark boundary

Current central modules:

- `src/core/capture`
- `src/core/replay`
- `src/gui/viewmodels`
- `src/gui/viewer`

Lower-signal or transitional modules:

- `src/core/block`
- `src/core/codec`
- `src/core/stream`
- `src/core/cxet_bridge`

If changing architecture:

- keep `core` as reusable backend
- do not move product logic into QML
- do not couple `hft-recorder` to non-public CXET internals

