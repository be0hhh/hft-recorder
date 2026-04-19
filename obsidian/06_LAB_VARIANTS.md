# Lab Variants And Support

Owns:

- Compression experiment definitions.
- Baseline codec wrappers.
- Custom codec families by stream type.
- Ranking and result comparison.

Main objects:

- `LabRunner`
- `RankingEngine`
- `ValidationRunner`
- `src/support/external_wrappers/*`
- `src/variants/trade/*`
- `src/variants/l1/*`
- `src/variants/orderbook/*`

Current state:

- The lab path exists, but much of it is still baseline or placeholder-level.
- `LabRunner` currently produces mostly scaffold/baseline results from `SessionCorpus`.
- Many support wrappers and variant implementations are present to define shape and targets, not yet a final optimized system.

Mental model:

- canonical session corpus stays the source of truth
- variants consume the corpus
- validation checks losslessness or quality
- ranking orders candidate pipelines

Depends on:

- `SessionCorpus`
- `PipelineDescriptor` and `PipelineResult`
- validation types
- external codec wrappers

Used by:

- `hft-recorder-bench`
- future lab/dashboard flows
- coursework comparison logic

Important current facts:

- `var01_*` directories are often anchor baselines, not final winners.
- `support/` is infrastructure for external codecs, not product logic.
- If a result looks too clean, check whether the implementation is scaffold-only before trusting it.

