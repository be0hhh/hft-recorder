# Capture Semantics And Schema Bridge

Purpose:

- Put the critical capture semantics in one note.
- Bridge streams, session schema, and replay assumptions.

Current capture truth:

- first supported live source is effectively Binance FAPI
- one `CaptureCoordinator` handles one symbol
- GUI batch capture is multi-coordinator, not one generic multi-symbol stream

Canonical files:

- `manifest.json`
- `trades.jsonl`
- `bookticker.jsonl`
- `depth.jsonl`
- `snapshot_000.json` and later snapshots

Channel semantics:

- `trades.jsonl`
  - stores normalized trade-like events
  - do not assume this is semantically identical to raw exchange `aggTrade`

- `bookticker.jsonl`
  - stores normalized L1 events

- `depth.jsonl`
  - stores normalized orderbook delta events
  - replay correctness depends on update-id continuity, not only JSON shape

- `snapshot_*.json`
  - stores full normalized snapshots
  - current replay model depends on snapshot bootstrap plus deltas

Schema rules that matter:

- canonical corpus contains normalized `CXETCPP` output, not raw exchange JSON
- numeric values stay integer-based
- schema stability matters more than exchange-specific payload fidelity

Replay assumptions that must stay explicit:

- snapshot bootstrap is mandatory for book reconstruction
- delta sequencing and gap handling are correctness requirements, not optional polish
- malformed or partially invalid corpus files must not be silently treated as healthy truth

Naming rules:

- keep `trade-like`, `bookticker`, `depth`, and `snapshot` distinct
- do not rename the active JSON-corpus path to match old `.cxrec` terms
