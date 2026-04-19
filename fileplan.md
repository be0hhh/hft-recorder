# hft-recorder implementation backlog

This file is the practical rollout plan for the active product path.

Current truth:
- canonical source of truth is the JSON session corpus;
- replay/viewer/capture stability comes before custom codecs;
- metrics and lab statistics must attach to stable backend contracts;
- new codec families start only after base integrity and stats seams are ready.

## Wave 1. Base hardening

Goal:
- stop treating damaged replay state as healthy truth.

Deliverables:
- deterministic capture finalize behavior after partial failure;
- explicit replay error details for malformed corpus input;
- orderbook continuity validation using `updateId` / `firstUpdateId`;
- replay integrity state exposed to backend callers.

Acceptance:
- malformed corpus reports deterministic errors;
- depth gaps with sequence ids are detected and fail replay open;
- replay cannot silently continue after integrity failure.

## Wave 2. Statistics preparation

Goal:
- prepare backend contracts for future metrics and lab results without shipping the full metrics feature yet.

Deliverables:
- internal metrics hook surface for capture, replay, validation, and lab;
- richer validation result shape with first mismatch and failure reason;
- richer pipeline result shape with ratio/speed/failure fields;
- current code paths emit backend metrics events at natural integration points.

Acceptance:
- no Prometheus/Grafana dependency yet;
- no UI metrics rollout yet;
- future lab/backend work can use stable result structs without redesign.

## Wave 3. Real lab baselines

Goal:
- replace placeholder lab output with real baseline measurements on canonical corpus.

Deliverables:
- baseline pipeline runs produce real `input_bytes`, `output_bytes`, `compression_ratio_ppm`;
- encode/decode timings and throughput fields are populated honestly;
- validation compares decoded output against canonical corpus exactly;
- ranking sorts real runs deterministically.

Acceptance:
- lab output is no longer scaffold-only;
- failed or unsupported pipelines are explicit;
- baseline results are stable on the same input corpus.

## Wave 4. Dashboard and session-facing statistics

Goal:
- make statistics visible after backend truth is stable.

Deliverables:
- `LabView` consumes real pipeline results;
- `DashboardView` shows ratio/speed/ranking summaries;
- capture/replay status text can surface integrity and benchmark summaries where useful.

Acceptance:
- GUI reads backend result contracts directly;
- no placeholder “skeleton only” messaging remains for active lab paths.

## Wave 5. Custom codec families

Goal:
- start real codec experiments only after the baseline path is trustworthy.

Deliverables:
- offline converter path from canonical JSON corpus to compact format candidates;
- exact decoder/replayer for lossless pipelines;
- benchmark comparison against baseline families by stream type.

Acceptance:
- custom codecs are judged by exactness first, then ratio, then decode speed, then encode speed;
- failed exactness blocks a codec from the primary ranking.

## Wave 6. Live-write and advanced observability

Goal:
- optional later phase after offline codec path proves itself.

Deliverables:
- evaluate live compressed writer path separately from the canonical writer;
- optional local `/metrics` or Prometheus-compatible surface if still justified;
- optional richer developer observability around capture/replay health.

Acceptance:
- canonical truth guarantees are preserved;
- live-write does not replace the stable offline path until proven safe.
