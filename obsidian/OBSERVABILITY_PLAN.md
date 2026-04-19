# Observability Plan

Purpose:

- Ground metrics and Grafana work in current product reality.

Decision:

- `metrics v1` is in scope
- `Grafana v2` is deferred

Why:

- capture/replay/viewer are real today
- metrics module is still a stub
- lab and reporting are not mature enough to justify a heavy observability stack

Metrics v1:

- keep GUI status and session reports as primary user-facing truth
- add an in-process metrics surface
- enable local `/metrics` only when configured
- no Pushgateway
- no remote collectors

Recommended v1 metrics:

- capture
  - `events_captured_total`
  - `bytes_written_total`
  - `write_errors_total`
  - `snapshot_fetch_failures_total`
  - `ws_reconnects_total`
  - `last_event_ts_ns`
  - `last_write_ts_ns`

- replay
  - `session_load_seconds`
  - `rows_loaded_total`
  - `replay_seek_count_total` or `replay_seek_seconds`
  - `replay_parse_failures_total`

- validation
  - `events_total`
  - `events_exact_match`
  - `events_mismatch`
  - `accuracy_ppm`
  - `validation_run_seconds`

- lab or bench
  - `input_bytes`
  - `output_bytes`
  - `compression_ratio_ppm`
  - `encode_ns`
  - `decode_ns`
  - `roundtrip_failed_total`
  - `peak_memory_bytes`

Rules:

- keep label cardinality small
- never use `session_id`, `event_index`, `update_id`, timestamps, or file paths as labels
- do not treat Prometheus as the canonical store for research results
- do not emit per-frame GUI telemetry into Prometheus

Grafana v2:

- one local Prometheus scrape target
- one dashboard for recorder health
- one dashboard for research results
- optional developer tool, not primary product workflow

Current mismatch to remember:

- `doc/LOGGING_AND_METRICS.md` describes a much richer surface than `src/core/metrics/Metrics.cpp` currently implements
