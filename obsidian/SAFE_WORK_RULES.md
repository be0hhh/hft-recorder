# Safe Work Rules

Purpose:

- Give one short operational summary before code changes.

Rules:

- `hft-recorder` consumes prebuilt `cxet_lib`; do not build parent internals into this repo
- no `add_subdirectory(..)` to parent `CXETCPP`
- no dependency on parent `network/`, `parse/`, `exchanges/`, or runtime internals
- current product truth is GUI-first, not CLI-first
- canonical truth artifact is the JSON session corpus, not `.cxrec`
- treat `trades` and `aggTrade` as different until code and docs prove otherwise
- capture, replay, and viewer are the active center; lab and metrics are not equally mature
- generated graph helps navigate symbols, not product truth
- if docs and code disagree, document the mismatch instead of smoothing it over

Current high-risk areas:

- capture contract looks generic, runtime binding is narrower
- replay parser is handwritten and fragile
- orderbook replay lacks explicit sequence-gap validation
- metrics docs outrun implementation

Safe default workflow:

1. read `START_HERE_TRUTH_ORDER.md`
2. check `CURRENT_REALITY_STATUS.md`
3. check `HISTORICAL_VS_ACTIVE.md`
4. then touch code or task notes
