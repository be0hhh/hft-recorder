# hft-recorder — External Libraries Catalog

Ready-made libraries for three roles:

1. **Baselines** — codecs we must benchmark *against* (proves the custom codec earns its keep).
2. **Reference implementations** — battle-tested codes we can study or directly link for entropy
   coders, integer codecs, SIMD primitives.
3. **Supporting infrastructure** — containers, atomics, logging, metrics, testing, benchmarking,
   allocators. These are NOT being benchmarked; they're the scaffolding around our core.

Every entry lists: what it is, why it matters for hft-recorder, license, current maintenance
state (as of 2026).

---

## A. General-purpose compression — baselines to beat

These are the "table-stakes" codecs. The bench must include them as reference points.

| Library | What | Why for us | License | Status |
|---|---|---|---|---|
| **zstd** (Facebook) | Dictionary + FSE/Huffman | Industry default for 2024+; has training-dictionary mode (`zdict`) which we can pre-compute on a captured corpus — this is the **hardest** baseline to beat. | BSD | actively maintained (v1.5.x) |
| **lz4** (Yann Collet) | LZ77 only, no entropy | Speed floor (~3 GB/s enc, ~5 GB/s dec). Shows what "pure dedup, no entropy" does. | BSD | stable |
| **zlib / miniz / libdeflate** | LZ77 + Huffman (gzip) | Legacy; `libdeflate` is the modern faster drop-in. One representative is enough. | zlib / Apache-2.0 | stable |
| **brotli** (Google) | LZ77 + custom entropy + static dictionary | Good middle-ground; static dict is interesting for market data (repeating exchange JSON keywords). | MIT | stable |
| **xz / LZMA / liblzma** | LZ77 + range coder | Highest ratio general-purpose; slow. Upper bound on "what generic compression can achieve". | PD | stable |
| **zstd --long** / **zstd --ultra -22** | extreme modes | Include in bench to show ratio ceiling for zstd. | BSD | — |

> WHY zstd is the critical baseline: if our custom codec isn't beating `zstd --ultra -22` with
> dictionary on market data, we haven't done anything worth shipping.

**Bench integration**: link zstd + lz4 + libdeflate + brotli + liblzma via `find_package` or
FetchContent; wrap each behind the `IBlockEncoder/Decoder` interface as "reference codecs" that
live in `src/core/codec/ref/*`. They do NOT participate in the recording file format
(`codec_id 0x01..0x07` is reserved for our custom pipeline), only in bench comparisons.

---

## B. Entropy-coder reference implementations (for the custom codecs)

If we want a known-correct oracle while writing AC_BIN16/CTX8/etc., these are the canonical
open-source implementations to study (and optionally fuzz against).

| Library | What | Why | License |
|---|---|---|---|
| **FSE** (Yann Collet) | Finite-State Entropy (tANS variant) | The same family as our rANS. FSE's C source is the cleanest ANS reference there is. Inside zstd (`lib/common/fse.c`). | BSD |
| **ryg_rans** (Fabian Giesen) | Scalar + SIMD rANS in a few hundred lines | The canonical pedagogical implementation of rANS. Good source for the quasi-adaptive rebuild pattern. | CC0 / public domain |
| **TurboANS / TurboRC** (powturbo) | Production rANS + range coder with AVX2/AVX-512 | Production-quality rANS that already beats FSE; use as an upper-bound target for our RANS_CTX8 performance. | GPL-3 (care with licensing) |
| **CppRangeCoder / subbotin** | Classic Subbotin range coder | Direct reference for our RANGE_CTX8. | public domain |
| **Eric Bodden — arithmetic coding tutorial** | Pedagogical binary AC | Good for unit-test oracles. | MIT |

> License note: TurboANS is GPL-3; if we link it, our binary becomes GPL-3. That's unacceptable
> for the course project's license model. Use only as **reference material**; do not link.

---

## C. Integer & columnar codecs (apply directly to `delta_price`, `delta_ts`, `delta_id`)

These take arrays of integers and pack them tight — exactly what our delta stage produces.
Critical: *any* of these could replace (or augment) our VarInt baseline after delta.

| Library | What | Why | License |
|---|---|---|---|
| **TurboPFor-Integer-Compression** (powturbo) | Fastest integer codec library: PFor, PFor-Delta, FOR, Variable-Byte, BP SIMD, Elias-Fano. >8 GB/s encode, >13 GB/s decode per recent benchmarks. | Direct competitor to our rANS for delta integers. **Bench it against RANS_CTX8**. | GPL-2 (read-only study for us; don't link) |
| **FastPFor** (Daniel Lemire) | SIMD PFor family in C++: `fastpfor`, `simdfastpfor`, `streamvbyte`. Academic-grade, Apache-2.0, linkable. | Directly usable in our bench tool as a reference integer codec. | Apache-2.0 |
| **streamvbyte** (Daniel Lemire) | SIMD VarInt: 4× faster than scalar VarInt. | Drop-in speedup for our VARINT codec path on the encoder side. | Apache-2.0 |
| **masked-vbyte** | SIMD VarInt decode | Narrower use; prefer streamvbyte. | Apache-2.0 |
| **SIMDComp** (Lemire) | SIMD bit-packing 1..32 bits | For columnar side-by-side packing of `delta_price`, `delta_ts` arrays. | Apache-2.0 |
| **roaring-bitmaps** (Lemire et al.) | Compressed bitmap sets | Niche: encode `bid_count`/`ask_count` masks on depth@0ms. | Apache-2.0 |

**Recommendation**: pull **FastPFor** + **streamvbyte** directly via FetchContent into the
bench tool. They become reference codecs in `src/core/codec/ref/pfor_codec.hpp`. TurboPFor we
study but don't link (GPL-2).

---

## D. Time-series / floating-point codecs (relevant even though we use int64 scaled)

Market prices are stored as int64 scaled, so Gorilla/Chimp/Elf apply only if we were working
in float. They are still worth studying because their XOR/trailing-zero tricks can inspire our
delta_ts encoding.

| Library | What | License |
|---|---|---|
| **Gorilla** (Facebook, original paper) | XOR-based streaming float compression: ~10× ratio on sensor-like series. Implementations: `burmanm/gorilla-tsc` (Java), `keisku/gorilla` (Go), `ghilesmeddour/gorilla-time-series-compression` (Python). | Apache-2.0 (most impls) |
| **Chimp / Chimp128** (VLDB 2022) | Improvement over Gorilla; +8.3% ratio on average. Several reference impls on GitHub. | Apache-2.0 |
| **Elf / Elf+** (VLDB 2023) | Current SOTA for lossless float time-series; +12.4% over Chimp. | Apache-2.0 |
| **ACTF** (ScienceDirect 2024) | Outperforms Chimp/Chimp128/Gorilla on multiple datasets. | research paper + reference C++ |
| **TurboFloat / TurboGorilla** (powturbo) | SIMD Gorilla-family in C with AVX-512. | GPL-2 |
| **zfp / fpzip** (LLNL) | Lossy/near-lossless float; scientific-sim use. | BSD |

Given hft-recorder uses int64 scaled, **Gorilla/Chimp/Elf go in bench only as baselines**
(re-interpret our int64 as double for a fair comparison — the paper's `double` pipeline is
what they report on). Not in our core codec.

---

## E. Helper libraries for the C++ core

### E.1 Containers & primitives

| Library | Use | License |
|---|---|---|
| **abseil-cpp** (Google) | `flat_hash_map`, `flat_hash_set`, `InlinedVector`, `StatusOr`, `Span`, `FixedArray`. | Apache-2.0 |
| **folly** (Facebook) | `ProducerConsumerQueue` (lock-free SPSC), `F14` maps, `IOBuf`. Can replace our hand-rolled SPSC if its performance proves better. | Apache-2.0 |
| **moodycamel::ConcurrentQueue / ReaderWriterQueue** | Header-only MPMC + SPSC. Good alternative if `folly` feels too heavy. | BSD / BSL |
| **Intel TBB / oneAPI TBB** | Parallel algorithms, `concurrent_hash_map`, work-stealing. Overkill for recorder but useful if bench wants multi-thread parallel runs. | Apache-2.0 |

### E.2 Atomics & lock-free building blocks

| Library | Use | License |
|---|---|---|
| **atomic_queue** (max0x7be) | Fast bounded lock-free SPSC/MPMC, header-only. | MIT |
| **DPDK ring** | (harvest the algorithm, not the whole DPDK) | BSD |
| **LMAX Disruptor (C++ port)** | Pattern; use only for reference. | Apache-2.0 |

### E.3 Hashing & CRC

| Library | Use | License |
|---|---|---|
| **xxhash** (Yann Collet) | Non-cryptographic hash; faster than CityHash on modern CPUs. | BSD |
| **crc32c (Google)** | SSE4.2 / ARMv8 CRC32C instruction wrappers — exactly what we need for `FileHeader`/`BlockHeader` CRC. Header-only. | BSD |
| **Intel ISA-L** | SIMD CRC + erasure codes | BSD-3 |

**Recommendation**: use `google/crc32c` for our CRC32C field — it's one header, uses the
hardware instruction, and is permissively licensed.

### E.4 Logging & metrics

| Library | Use | License |
|---|---|---|
| **spdlog** | Logging wrapper target (see `LOGGING_AND_METRICS.md`). | MIT |
| **fmt** (fmtlib) | Formatted strings; auto-pulled by spdlog. | MIT |
| **prometheus-cpp** | Metrics export + Pushgateway client. | MIT |

### E.5 Testing & benchmarking

| Library | Use | License |
|---|---|---|
| **GoogleTest** | Default test framework. | BSD |
| **GoogleBenchmark** | Microbench target `hftrec_benchmarks`. | Apache-2.0 |
| **rapidcheck** | Property-based tests (codec round-trip properties). | BSD |
| **Catch2** | Alternative to GTest if someone prefers header-only. | BSL-1.0 |
| **HdrHistogram_c** | P50/P99/P999 latency distributions — use for writer flush and SPSC latency histograms. | BSD |
| **nanobench** (martinus) | Header-only microbench, lighter than Google Benchmark. | MIT |

### E.6 Allocators (optional, evaluate in bench)

| Library | Use | License |
|---|---|---|
| **mimalloc** (Microsoft) | Drop-in replacement `malloc`; often 5–20% speedup on many-allocation workloads. | MIT |
| **jemalloc** | Facebook/BSD standard. | BSD |
| **tcmalloc** | Google; well-tuned for short-lived allocations. | BSD |

We allocate ~nothing in hot path, so allocator choice mostly affects startup. Still worth a
bench sweep once codec implementations settle.

### E.7 SIMD abstraction (for rANS AVX2)

| Library | Use | License |
|---|---|---|
| **Google Highway** | Portable SIMD wrapper (AVX2/AVX-512/NEON). Good if we want to compile the same code on aarch64 later. | Apache-2.0 |
| **xsimd** | Alternative portable SIMD; template-heavy. | BSD-3 |
| **Intel ISPC** (compiler) | Separate compiler for SIMD kernels. Overkill here. | BSD-3 |

For our `AcBin16Ctx8` and `RansCtx8` AVX2 paths, either hand-written intrinsics OR Highway are
fine. Highway gains portability; intrinsics give explicit control.

### E.8 I/O

| Library | Use | License |
|---|---|---|
| **liburing** | io_uring async disk I/O on Linux 5.1+. Potential speedup for writer if `pwrite + fsync` becomes the bottleneck. | MIT |
| **folly IOBuf** | Chained buffers for zero-copy encoding. | Apache-2.0 |

For MVP: stick with `pwrite + fsync`. io_uring can be considered if writer queues get deep.

---

## F. Python research stack (for Layer 2 — fast iteration)

Python code lives in `ML/` or a separate `apps/hft-recorder/research/` notebook dir. Reads
captured `.cxrec` files, prototypes ideas, produces plots.

### F.1 File I/O + numerical

| Library | Use |
|---|---|
| **numpy** | Core array type; every codec prototype consumes/produces `ndarray`. |
| **pandas** | DataFrame for analysis; slower than polars but the ML ecosystem runs on it. |
| **polars** | Modern DataFrame with Arrow backend; much faster than pandas. |
| **pyarrow** | Arrow + Parquet read/write; Parquet with ZSTD/LZ4 codecs = good baseline. |
| **zarr** (v3) | Chunked N-dim arrays with codec pipeline. |

### F.2 Compression bindings

| Package | Wraps | License |
|---|---|---|
| **zstandard** | libzstd | BSD |
| **python-lz4** | lz4 | BSD |
| **python-snappy** | snappy | Apache-2.0 |
| **brotli** (python wheel) | brotli | MIT |
| **pybz2 / bz2** (stdlib) | bzip2 | stdlib |
| **lzma** (stdlib) | xz/lzma | stdlib |
| **zfpy** | zfp lossy float | BSD |
| **fpzip** | fpzip | BSD |
| **blosc2** | Meta-codec: composes zstd/lz4/blosc + SHUFFLE/BITSHUFFLE filters. Best-in-class for numpy arrays of numbers. | BSD |
| **numcodecs** (Zarr ecosystem) | Codecs: delta, shuffle, quantize, fletcher32, zstd, lz4, blosc, vlen-utf8. Directly composable. | MIT |
| **bitshuffle** | Bit transpose filter that dramatically boosts zstd/lz4 on numeric arrays. Essential to try in prototype stage. | LGPL-3 |

> **blosc2 + numcodecs** is the combo that lets you try `delta → bitshuffle → zstd` in three
> lines of Python — **start every prototype there**.

### F.3 Time-series specific

| Package | What |
|---|---|
| **gorillacompression** (pypi) | Python port of Gorilla. |
| **chimppy** / `chimp-python` (research repos, no PyPI package yet) | Chimp reference impls. |
| **pyts** | Time-series classification & transforms (more ML than compression but useful for feature analysis). |
| **tsfresh** | Feature extraction; not compression, but helps understand structure. |

### F.4 Tick-data specific storage

| Package | What | License |
|---|---|---|
| **arcticdb** (Man Group / Bloomberg, open-sourced 2023, 6.11.0 released 2026-03-23) | HFT-scale columnar DataFrame DB with internal compression. Directly relevant: this is what *production* quant shops use to store market tick data. Read their codec selection choices as a reference. | BSL-1.1 |
| **arctic** (classic, pre-ArcticDB) | Older Man Group implementation; uses MongoDB. | LGPL-2.1 |
| **questdb** (Python client) | Time-series DB with its own compression. | Apache-2.0 |
| **clickhouse-driver** (Python) | ClickHouse uses ZSTD/LZ4/DOUBLE_DELTA/T64 per-column. Worth studying their compressed MergeTree engine for column-codec ideas. | MIT |
| **influxdb-client** (Python) | InfluxDB's TSM engine uses Gorilla-like encoding. | MIT |

### F.5 Benchmarking & plotting

| Package | Use |
|---|---|
| **matplotlib** | Bar charts for ratio comparisons. |
| **seaborn** | Nicer defaults over matplotlib. |
| **plotly** | Interactive Grafana-adjacent plots. |
| **pytest-benchmark** | Consistent microbench harness. |
| **hypothesis** | Python property-based testing (analog of rapidcheck). |

---

## G. HFT / market-data reference systems (study, don't link)

| System | What to learn |
|---|---|
| **ArcticDB** (Man Group + Bloomberg) | Column layouts, codec choice per column type, Parquet fallback. |
| **kdb+/q** (KX) | Industry standard for tick data; proprietary. Read public blog posts on columnar compression. |
| **ClickHouse** | Per-column `CODEC(Delta, DoubleDelta, T64, Gorilla, FPC, ZSTD, LZ4)` syntax. The per-column pipeline approach we're implementing is essentially this. |
| **InfluxDB TSM** | Per-data-type encoding selection (time → delta-delta, float → Gorilla). |
| **QuestDB** | Column-oriented, SIMD-first compression. |
| **TimescaleDB** | Postgres extension; uses Gorilla + delta-delta. |
| **Apache IoTDB** | `TS_2DIFF`, `GORILLA`, `CHIMP`, `ZIGZAG` encodings selectable per-column. |

The pattern in all of them: **one generic codec is never the right answer**; stack a type-aware
transform (delta, delta-delta, Gorilla) in front of a general-purpose entropy coder (zstd/LZ4).
This is exactly our architecture — validates the approach.

---

## H. Recommended picks for hft-recorder (tl;dr)

If you want a single minimal "what should be in `CMakeLists.txt` today" list:

```
# Core (linked into hft-recorder binaries)
find_package(absl CONFIG REQUIRED)                 # flat_hash_map + InlinedVector
find_package(spdlog CONFIG REQUIRED)                # logging
find_package(fmt CONFIG REQUIRED)                   # depended on by spdlog
find_package(prometheus-cpp CONFIG REQUIRED)        # metrics + push
FetchContent_Declare(crc32c ... google/crc32c)     # hardware CRC32C

# Bench-only (linked into hft-recorder-bench)
find_package(zstd CONFIG REQUIRED)                  # headline baseline
find_package(lz4 CONFIG REQUIRED)                   # speed floor baseline
find_package(Brotli CONFIG REQUIRED)                # static-dict baseline
find_package(LibLZMA REQUIRED)                      # high-ratio baseline
FetchContent_Declare(FastPFor ... lemire/FastPFor) # integer-codec baseline
FetchContent_Declare(streamvbyte ... lemire/streamvbyte)

# Test / bench tooling (not linked into release binaries)
find_package(GTest CONFIG REQUIRED)
find_package(benchmark CONFIG REQUIRED)
FetchContent_Declare(rapidcheck ... emil-e/rapidcheck)
FetchContent_Declare(HdrHistogram_c ... HdrHistogram/HdrHistogram_c)
```

Python research side (`requirements.txt` for `research/` dir):

```
numpy pandas polars pyarrow
zstandard python-lz4 brotli
blosc2 numcodecs bitshuffle zfpy fpzip
gorillacompression                    # for time-series baselines
arcticdb                              # for real-scale storage comparison
matplotlib seaborn plotly
pytest-benchmark hypothesis
```

---

## I. Anti-recommendations (do NOT use)

- **TurboPFor / TurboANS** — GPL-2/-3; linking contaminates our license. Study only.
- **libcurl** — we do NOT do HTTP ourselves (CXETCPP owns that). Banned.
- **rapidjson / nlohmann::json** — CXETCPP uses simdjson; we have no JSON at all in hft-recorder.
- **protobuf / flatbuffers / cap'n proto** — our file format is explicit `#pragma pack(1)`;
  adding a serialization framework is contrary to the whole point of the exercise.
- **boost.asio in app code** — CXETCPP already wraps Beast; don't add a parallel async stack.
- **tbb::concurrent_queue** for the producer→writer path — SPSC rings are enough; MPMC overhead
  is wasted here.

---

## J. How we use this catalog

1. **Phase 2 (VARINT recorder)**: only abseil + spdlog + fmt + prometheus-cpp + crc32c. Nothing else.
2. **Phase 3 (bench tool)**: add zstd + lz4 + brotli + lzma + FastPFor + streamvbyte as reference
   codecs in `src/core/codec/ref/`. Wrap behind `IBlockEncoder`. Their `codec_id` in bench output
   is 0x80+ (reserved range, not written to `.cxrec` files).
3. **Phase 5 (Grafana)**: the 28-cell matrix (§ `BENCHMARK_PLAN.md`) expands to ~60 cells once
   reference codecs are added. Grouped chart: "our 7" vs "reference 8" per stream.
4. **Python research (any time)**: prototype an idea in blosc2/numcodecs first (5 lines of
   Python). If it shows a ≥10% improvement over zstd on a stream, graduate it to a C++ codec
   behind an `IBlockEncoder`.

---

## References

- `CODEC_VARIANTS.md` — our 7 custom codecs (what we're benchmarking *against* these libs).
- `BENCHMARK_PLAN.md` — 28-cell measurement grid; this doc extends it with external codecs.
- `COMPARISON_MATRIX.md` — earlier research version of the same idea.
- `CUSTOM_IDEA_CATALOG.md` — list of transform ideas; many can use these libraries as components.
- `CODING_STYLE.md` § E.4 — the spdlog/fmt wrapper rule.
