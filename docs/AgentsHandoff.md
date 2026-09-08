# hft-recorder agent handoff guide

Current truth:
- `hft-recorder` is now GUI-first
- canonical storage is JSON corpus per session
- replay and validation come before serious compression claims

## What is already in the tree

- old CLI-first scaffold still exists under `src/Process/src/`
- compression-variant scaffold still exists under `src/Lab/Variants/`
- new backend skeleton now exists under:
  - `src/Runtime/src/Capture/`
  - `src/Runtime/src/corpus/`
  - `src/Runtime/src/Validation/`
  - `src/Lab/src/`
  - `src/Runtime/src/Capture/Bridge/`
- new Qt/QML shell now exists under:
  - `src/Gui/src/`

## What to treat as source of truth

Read first:
1. `docs/Overview.md`
2. `docs/SessionCorpusFormat.md`
3. `docs/GuiProduct.md`
4. `docs/ImplementationPlan.md`
5. `docs/Architecture.md`
6. `docs/ValidationAndRanking.md`

## Current recommended order of implementation

1. Finish `src/Runtime/src/Capture/` until a session directory and manifest can be created correctly.
2. Wire real normalized JSON serialization for trades, bookticker, depth, and snapshots.
3. Finish `src/Runtime/src/corpus/` loading of those files.
4. Finish `src/Gui/src/` viewmodels and page wiring for capture and session browsing.
5. Finish `src/Runtime/src/Validation/` and connect it to the validation page.
6. Only then push harder on `src/Lab/src/` and `src/Lab/Variants/`.

## Important caution

- Old `.cxrec` and block/codec docs remain historical references, not immediate
  implementation truth.
- Do not let the old CLI-first scaffold dictate the new architecture.
