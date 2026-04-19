# Build Run Test Matrix

Purpose:

- Separate parent-repo responsibilities from nested-repo responsibilities.
- Keep agents from building or running the wrong thing.

Ownership:

- parent `CXETCPP`
  - owns library implementation
  - owns exchange integrations
  - owns transport/runtime internals
  - produces `libcxet_lib.so`

- nested `apps/hft-recorder`
  - owns GUI, capture orchestration, replay, validation, lab scaffold, and docs
  - links against prebuilt `cxet_lib`

Build matrix:

- parent build
  - result: `libcxet_lib.so` and public headers
  - reason: recorder dependency surface

- recorder build
  - result: GUI, support CLI, tests
  - reason: app-layer work only

Runtime matrix:

- `hft-recorder-gui`
  - status: primary product entrypoint
  - trust: active

- `hft-recorder capture`
  - status: secondary support CLI
  - trust: usable but scoped

- `hft-recorder-bench`
  - status: support binary
  - trust: scaffold

- `analyze`
  - status: placeholder
  - trust: do not plan around it

- `report`
  - status: placeholder
  - trust: do not plan around it

Testing matrix:

- `tests/unit/*`
  - trust: active smoke/regression coverage for current JSON path

- `tests/integration`
  - trust: boundary exists; coverage is limited

- `tests/bench`
  - trust: support boundary only

Do not assume:

- that recorder rebuilds parent `CXETCPP`
- that all listed binaries are production-ready
- that metrics or lab placeholders reflect implemented runtime behavior
