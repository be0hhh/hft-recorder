# Historical Vs Active

Purpose:

- Keep agents from mixing current JSON-corpus work with older `.cxrec` material.

Active truth:

- `doc/OVERVIEW.md`
- `doc/SESSION_CORPUS_FORMAT.md`
- `doc/GUI_PRODUCT.md`
- `doc/ARCHITECTURE.md`
- `doc/STREAMS.md`
- `doc/TESTING_CONTRACT.md`
- `obsidian/CURRENT_REALITY_STATUS.md`
- `obsidian/CAPTURE_SEMANTICS_AND_SCHEMA_BRIDGE.md`

Current active architecture:

- GUI-first product
- canonical truth = JSON session corpus
- capture -> replay/viewer -> validation -> lab
- prebuilt `cxet_lib` consumer boundary

Historical or caution-zone material:

- `doc/FILE_FORMAT.md`
- `doc/DELTA_ENCODING.md`
- `doc/ARITHMETIC_CODING.md`
- `doc/ENCODE_WALKTHROUGH.md`
- parts of `doc/ERROR_HANDLING_AND_GAPS.md`
- parts of `doc/LOGGING_AND_METRICS.md`
- placeholder CLI text around `.cxrec`

Why this matters:

- some older docs describe a `.cxrec` / block-format / CLI-first path
- some newer docs describe a GUI-first JSON-corpus path
- code currently matches the second path much more than the first

Practical reading rule:

- use historical docs for ideas, terminology, and future candidates
- do not treat them as current implementation truth unless a newer note points to them explicitly

Common failure modes:

- assuming `analyze` or `report` CLI is real because the binary exists
- planning around block/codec internals instead of JSON corpus
- trusting metrics docs as implemented runtime behavior
- reading generated graph as if it proved production readiness
