# src/ - agent handoff guide

Current truth:
- `hft-recorder` is now GUI-first
- canonical storage is JSON corpus per session
- replay and validation come before serious compression claims

## What is already in the tree

- old CLI-first scaffold still exists under `src/app/`
- compression-variant scaffold still exists under `src/variants/`
- new backend skeleton now exists under:
  - `src/core/capture/`
  - `src/core/corpus/`
  - `src/core/validation/`
  - `src/core/lab/`
  - `src/core/cxet_bridge/`
- new Qt/QML shell now exists under:
  - `src/gui/`

## What to treat as source of truth

Read first:
1. `doc/OVERVIEW.md`
2. `doc/SESSION_CORPUS_FORMAT.md`
3. `doc/GUI_PRODUCT.md`
4. `doc/IMPLEMENTATION_PLAN.md`
5. `doc/ARCHITECTURE.md`
6. `doc/VALIDATION_AND_RANKING.md`

## Current recommended order of implementation

1. Finish `src/core/capture/` until a session directory and manifest can be created correctly.
2. Wire real normalized JSON serialization for trades, bookticker, depth, and snapshots.
3. Finish `src/core/corpus/` loading of those files.
4. Finish `src/gui/` viewmodels and page wiring for capture and session browsing.
5. Finish `src/core/validation/` and connect it to the validation page.
6. Only then push harder on `src/core/lab/` and `src/variants/`.

## Important caution

- Old `.cxrec` and block/codec docs remain historical references, not immediate
  implementation truth.
- Do not let the old CLI-first scaffold dictate the new architecture.
