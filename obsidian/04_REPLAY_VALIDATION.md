# Replay And Validation

Owns:

- Loading session files from disk.
- Parsing JSON corpus rows.
- Building a merged event timeline.
- Reconstructing current book state at a viewport timestamp.
- Comparing original vs decoded outputs for compression validation.

Main objects:

- `SessionReplay`
- `JsonLineParser`
- `BookState`
- `CorpusLoader`
- `SessionCorpus`
- `ValidationRunner`

Replay flow:

1. `SessionReplay::open()` or the `add*File()` methods read session artifacts.
2. `JsonLineParser` converts JSON text into `TradeRow`, `BookTickerRow`, `DepthRow`, and snapshot documents.
3. `SessionReplay::finalize()` builds a merged `events()` timeline and timestamp bounds.
4. `seek(ts)` rewinds or advances `BookState` so the viewer sees the correct book at that time.

Validation flow:

1. Lab or codec code produces decoded output.
2. `ValidationRunner::compare()` checks original vs decoded sequences.
3. Result is stored in `ValidationResult` and later ranked or surfaced in reports.

Depends on:

- canonical JSON session schema
- filesystem
- replay row structs and book state logic

Used by:

- `ChartController`
- future validation view and lab path

Important current facts:

- Replay path is real and central.
- Validation exists, but the visible product center today is replay/viewer more than a full benchmark dashboard.
- `SessionCorpus` is the bridge from disk truth to lab experiments.

