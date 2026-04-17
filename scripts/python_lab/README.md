# hft-recorder — Python research lab

Sandbox for fast prototyping of compression ideas before porting the winners into
C++. Not part of the build; not linked anywhere. Lives here purely so `doc/
RESEARCH_PROGRAM.md` Track D has a concrete home.

## Requirements

- Python 3.11+
- A virtualenv; install with `pip install -r requirements.txt`

## Layout

- `baselines/`  — zstd / lz4 / brotli / gzip prototypes
- `transforms/` — delta / zigzag / dictionary / columnar experiments
- `orderbook/`  — orderbook-specific ideas (topN, keyframe, reconstruction-first)
- `notebooks/`  — Jupyter notebooks kept in sync with experiments
- `reports/`    — CSVs, markdown write-ups, ratio tables

## Promotion rule

Python wins are only candidates. Ideas promoted to C++ land in
`src/variants/<family>/varNN_*_cpp/` with a matching `varNN_*_pyproto`
reference left here so the write-up survives.
