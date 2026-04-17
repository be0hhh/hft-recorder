# hft-recorder — Configuration & CLI

The recorder reads configuration from a `.env` file in the working directory; CLI flags
override individual keys when given. The bench tool takes its inputs exclusively from CLI.

---

## `.env.example`

```ini
# ─── Exchange credentials ────────────────────────────────────────────────
BINANCE_API_KEY=
BINANCE_SECRET=
# Proxy only if required; leave empty otherwise.
PROXY=

# ─── What to record ─────────────────────────────────────────────────────
SYMBOL=BTCUSDT                   # one symbol per recorder process for MVP
EXCHANGE=binance                 # binance | bybit | okx (MVP: binance only)
MARKET=futures_usd               # futures_usd | spot (MVP: futures_usd)
DURATION_SEC=1800                # 30 min — enough entropy for 28-cell bench
OUTPUT_DIR=./recordings          # relative to CWD; created if missing
CODEC=VARINT                     # VARINT only during recording; bench re-encodes

# ─── Snapshot cadence ───────────────────────────────────────────────────
SNAPSHOT_INTERVAL_SEC=60         # Binance-recommended full-depth refresh interval

# ─── Logging ────────────────────────────────────────────────────────────
LOG_LEVEL=info                   # trace | debug | info | warn | error
LOG_DIR=./logs                   # rotating files; 100 MB × 7 retained

# ─── Metrics ────────────────────────────────────────────────────────────
PROMETHEUS_PUSHGATEWAY_URL=      # e.g. http://localhost:9091 ; empty = disable
PROMETHEUS_PUSH_INTERVAL_SEC=10

# ─── CPU affinity (Linux only, ignored on non-Linux) ────────────────────
CPU_MAIN=0
CPU_CONTROL=7
CPU_PROD_TRADES=2
CPU_PROD_BOOKTICK=3
CPU_PROD_DEPTH=4
CPU_PROD_SNAP=5
CPU_WR_TRADES=8
CPU_WR_BOOKTICK=9
CPU_WR_DEPTH=10
CPU_WR_SNAP=11

# ─── Writer tuning (advanced; defaults in constants.hpp) ────────────────
BLOCK_MAX_EVENTS=512
BLOCK_MAX_WALLTIME_MS=1000
BLOCK_MAX_BYTES=262144           # 256 KB
CODER_RESET_EVERY_BLOCKS=1024
FSYNC_EVERY_BLOCKS=16
```

---

## `.env` loading rules

- Loaded by `src/core/config/EnvLoader.cpp` — simple `KEY=VALUE` parser (one per line, `#`
  starts a comment, no quoting, no shell expansion, whitespace around `=` ignored).
- **Missing `.env`** is fatal (exit code 2). There is no baked-in fallback because the recorder
  is useless without a symbol + exchange.
- **Missing key** is fatal unless the key has a default constant (see table below).
- **Malformed line** (no `=` after stripping comment) is fatal.
- Values are always strings; the loader provides typed accessors: `envString`, `envInt`,
  `envBool`, `envPath`, `envDuration`.

| Key | Required? | Default | Source |
|---|---|---|---|
| `BINANCE_API_KEY`, `BINANCE_SECRET` | only for private endpoints (future) | `""` | `.env` |
| `SYMBOL` | yes | — | `.env` or `--symbol` |
| `EXCHANGE`, `MARKET` | yes | — | `.env` |
| `DURATION_SEC` | yes | `1800` if omitted | `.env` or `--duration` |
| `OUTPUT_DIR` | no | `./recordings` | `.env` or `--output` |
| `CODEC` | no | `VARINT` | `.env` or `--codec` |
| `SNAPSHOT_INTERVAL_SEC` | no | `60` | `.env` |
| `LOG_LEVEL` | no | `info` | `.env` |
| `LOG_DIR` | no | `./logs` | `.env` |
| `PROMETHEUS_*` | no | empty → disabled | `.env` |
| `CPU_*` | no | defaults in `constants.hpp` | `.env` |
| `BLOCK_*`, `FSYNC_EVERY_BLOCKS` | no | defaults in `constants.hpp` | `.env` |

---

## `hft-recorder` CLI

Intended usage: read `.env`, no flags needed. Flags exist for one-off overrides.

```
hft-recorder [--symbol SYM] [--exchange EX] [--market MKT]
             [--duration SEC] [--output DIR] [--codec NAME]
             [--dry-run] [--help] [--version]
```

- `--dry-run` — builds pipelines, opens WS connections, but discards events instead of writing.
  Used to verify configuration + connectivity.
- `--version` — prints linked CXETCPP version + recorder commit hash; exits 0.
- Flags override corresponding `.env` values. Unknown flags → exit 2.

Exit codes: see `ERROR_HANDLING_AND_GAPS.md` § "Exit codes".

### Typical invocation

```bash
# Record 30 min BTCUSDT futures on Binance with VARINT (default).
./run.sh record

# Record a different symbol for 10 min, write to /data/rec.
./bin/hft-recorder --symbol ETHUSDT --duration 600 --output /data/rec

# Connectivity smoke test.
./bin/hft-recorder --dry-run --duration 30
```

---

## `hft-recorder-bench` CLI

Offline tool that re-encodes / round-trips a captured `.cxrec`.

```
hft-recorder-bench <input.cxrec>
                   --codec {all|varint|ac_bin16_ctx0|ac_bin16_ctx8|ac_bin16_ctx12|
                            ac_bin32_ctx8|range_ctx8|rans_ctx8}
                   [--roundtrip]       # decode → compare → must be bit-exact
                   [--repair]          # scan-forward recovery on CRC errors
                   [--push]            # push metrics to Pushgateway (URL from env)
                   [--output FILE]     # if the codec differs from input, write re-encoded file
                   [--blocks N]        # stop after N blocks (for quick smoke tests)
                   [--help]
```

- `--codec all` iterates all seven codecs sequentially (28 ratio measurements when fed
  each of the 4 stream types).
- `--roundtrip` is **the correctness gate** — every codec must pass round-trip before being
  trusted on the recorded data. Failure exits with code 5.
- `--push` uses `PROMETHEUS_PUSHGATEWAY_URL` from env.

---

## Signal handling

Single `std::atomic<bool> g_stop{false}`. Handler installed in `main()`:

```cpp
static void onSignal(int) noexcept { g_stop.store(true, std::memory_order_release); }

int main(...) {
    std::signal(SIGTERM, &onSignal);
    std::signal(SIGINT,  &onSignal);
    // SIGHUP, SIGQUIT → default (terminate) on Linux; we don't rely on them.
    ...
}
```

Rules:

- Every long-running loop polls `g_stop.load(std::memory_order_relaxed)` at least once per
  iteration.
- Producers exit after the current `tryPop` call returns.
- Writers finalise the current block (flush + fsync + close) before exiting.
- No `longjmp`, no `std::terminate`, no exceptions crossing thread boundaries.
- On second SIGINT within 2 s → hard-kill via `std::_Exit(130)`. WHY: stuck writer on slow disk
  should not block Ctrl-C forever.

---

## Default constants (in `src/core/common/constants.hpp`)

All numeric defaults are compile-time constants with names `kCamelCase`. Env keys override at
process start; constants are the ground truth when env is missing.

```cpp
namespace hftrec {

inline constexpr uint32_t kFileMagic                 = 0x43525843u;    // "CXRC"
inline constexpr uint32_t kBlockMagic                = 0x004B4C42u;    // "BLK\0"
inline constexpr uint16_t kFileVersion               = 1u;

inline constexpr std::size_t kMaxEventsPerBlock      = 512u;
inline constexpr std::size_t kMaxBlockBytes          = 256u * 1024u;   // 256 KB
inline constexpr std::chrono::milliseconds kMaxBlockWallTime{1000};

inline constexpr std::size_t kCoderResetEveryBlocks  = 1024u;
inline constexpr std::size_t kFsyncEveryBlocks       = 16u;

inline constexpr uint32_t kAcMaxCount                = 4096u;          // halving threshold
inline constexpr uint32_t kRansM                     = 1u << 14;       // 16384
inline constexpr std::size_t kProducerRingCapacity   = 4096u;          // SPSC size
inline constexpr std::size_t kSnapshotIntervalSec    = 60u;

inline constexpr std::size_t kEmptyPopSleepMicros    = 200u;           // main tryPop loop
inline constexpr std::size_t kWatchdogTickMs         = 1000u;
inline constexpr std::size_t kShutdownJoinTimeoutMs  = 30'000u;
inline constexpr std::size_t kMetricsPushIntervalSec = 10u;

// Default CPU affinity (overridden by env).
inline constexpr int kCpuMain          = 0;
inline constexpr int kCpuControl       = 7;
inline constexpr int kCpuProdTrades    = 2;
inline constexpr int kCpuProdBookTick  = 3;
inline constexpr int kCpuProdDepth     = 4;
inline constexpr int kCpuProdSnap      = 5;
inline constexpr int kCpuWrTrades      = 8;
inline constexpr int kCpuWrBookTick    = 9;
inline constexpr int kCpuWrDepth       = 10;
inline constexpr int kCpuWrSnap        = 11;

} // namespace hftrec
```

Every magic number used anywhere in `src/core/` **must** be named here. `grep -n 'return [0-9]'
src/core/` should not reveal bare numeric literals beyond 0 and 1.

---

## `run.sh` subcommands

The helper script in `apps/hft-recorder/` drives WSL-based builds and invocations.

| Subcommand | Action |
|---|---|
| `./run.sh install-cxet` | Build + install CXETCPP to `~/.local/cxet`. Runs **once**. |
| `./run.sh build` | CMake configure + build `hft-recorder` + `hft-recorder-bench` in WSL. |
| `./run.sh record` | Run the recorder using values from `.env`. |
| `./run.sh record ETHUSDT 300` | Override symbol + duration. |
| `./run.sh bench <file> [codec]` | Run bench; `codec` optional (default `all`). |
| `./run.sh rt <file> <codec>` | Round-trip one codec. |
| `./run.sh clean` | Delete `build/` and `recordings/` (prompts first). |
| `./run.sh help` | Show subcommands. |

All subcommands run inside WSL (`wsl -d Ubuntu-24.04 -e bash -c '...'`) because CXETCPP builds
Linux-only. Windows paths (`C:\...`) are never passed to cmake; everything is `/mnt/c/...`.

---

## References

- `CODING_STYLE.md` § "Constants" — naming rule for magic numbers.
- `API_CONTRACTS.md` § `WriterConfig` — runtime-passed overrides.
- `ERROR_HANDLING_AND_GAPS.md` § "Exit codes" — what each error returns.
- `apps/arbitrage-screener/run.sh` — reference for the WSL install + build shape.
