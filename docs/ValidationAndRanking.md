# hft-recorder - validation and ranking

## Validation goal

Every compression pipeline must be evaluated against the canonical corpus.

The default standard is:
- exact normalized replay

This means:
- decoded records must match the normalized source records
- comparison is done on parsed/normalized values, not on raw exchange JSON

## Validation outputs

Per run, the validator produces:
- `events_total`
- `events_exact_match`
- `events_mismatch`
- `accuracy_fraction`
- `first_mismatch_channel`
- `first_mismatch_event_index`

## Accuracy classes

- `lossless_exact`
- `near_lossless`
- `lossy_experimental`
- `failed`

Only `lossless_exact` pipelines are allowed into the primary coursework ranking.

## Benchmark metrics

Per pipeline run:
- `input_bytes`
- `output_bytes`
- `compression_ratio`
- `encode_ns`
- `decode_ns`
- `encode_mb_per_sec`
- `decode_mb_per_sec`
- `accuracy_class`
- `accuracy_fraction`

## Ranking rules

Rankings are separated by:
- stream family
- profile

Profiles:
- `archive`
- `replay`
- `online_candidate`

Default winner ordering:
1. exactness
2. compression ratio
3. decode speed
4. encode speed

There is no single universal winner for all data families.
