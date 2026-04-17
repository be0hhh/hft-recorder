# hft-recorder — Coding Style

Concrete C++ rules for everything under `apps/hft-recorder/src/` and `bench/`.
These are project-local; the parent `CXETCPP/CLAUDE.md` has stricter rules for
the library itself, and this document deliberately mirrors them wherever the
recorder lives in the same hot path.

If a rule here conflicts with something in a variant's own `README.md`, this
document wins — variants are not allowed to relax these rules without a
written justification in their README.

---

## 1. Language and toolchain

- **C++23**, `-std=c++23`.
- **Linux x86_64 only.** WSL Ubuntu 24.04 LTS is the reference build environment.
- Build in WSL only (`/mnt/c/...` paths). Windows paths do not work with CMake/clangd.
- Compiler: clang 18+ or gcc 13+. Clang is the primary target.
- **No exceptions, no RTTI** in hot-path code (`src/core/codec/`, `src/core/block/`, `src/core/online/`, any `src/variants/*/encode.cpp` or `decode.cpp`). Application-layer code under `src/app/` may use exceptions for startup/CLI errors only.
- `-Wall -Wextra -Werror -Wconversion` in `Debug`; same flags without `-Werror` in `Release`.
- No warnings suppression with `#pragma` — fix the cause.

---

## 2. Numeric primitives (MANDATORY)

Everywhere the recorder handles market data it must use the CXETCPP primitives.
Raw `double` / `float` / bare `int64_t` for prices, quantities, or timestamps
is **banned**.

| Use | Do | Don't |
|-----|----|-------|
| Price | `cxet::Price p; p.raw = ...;` (int64 scaled 1e8) | `double p = 0.0;` |
| Amount / Quantity | `cxet::Amount qty; qty.raw = ...;` | `float qty = 0.0f;` |
| Timestamp | `cxet::TimeNs ts; ts.raw = ...;` (ns since epoch, int64) | `int64_t ts_ms = ...;` |
| Id / sequence id | `cxet::Id id; id.raw = ...;` (uint64) | `uint64_t id = ...;` |
| Counter / count | `cxet::CountVal c; c.raw = ...;` (uint32) | `size_t n = ...;` when the count is exchange-native |
| Symbol | `cxet::Symbol s; s.copyFrom("BTCUSDT");` (fixed 16-byte buffer) | `std::string s = "BTCUSDT";` in the hot path |
| Non-owning view | `cxet::Span<const T>` | raw `T* + size_t` pair |
| Timeframe tag | `cxet::TimeframeBuf tf; tf.copyFrom("1m");` | `std::string tf;` |

Conversion to display types (`double` for a chart, `uint64_t ms` for logs) is
allowed **at the edge** — never in the encode/decode path. Example edge:

```cpp
// OK — converting at UI edge
double displayPrice = static_cast<double>(trade.price.raw) / 1e8;
qint64 tsMs         = trade.timestamp.raw / 1'000'000LL;
```

Rationale: prices from CXETCPP are already round-trip identical int64 across
every exchange; going through `double` loses the last two tick digits on some
pairs and breaks byte-for-byte round-trip tests. The format spec in
[FILE_FORMAT.md](FILE_FORMAT.md) is defined over the int64 raw values, so
changing representation mid-pipeline is a correctness bug, not a style issue.

---

## 3. Containers in the hot path

Hot path = everything called per-event or per-block during capture or decode.
Cold path = startup, CLI parsing, one-off config.

| Container | Hot path | Cold path |
|-----------|----------|-----------|
| `std::vector<T>` | **banned** | OK for one-off setup |
| `std::unordered_map` | **banned** | OK for one-off setup |
| `std::string` | **banned** | OK for CLI/env |
| `absl::InlinedVector<T, N>` (from `extra/abseil-cpp`) | preferred for bounded small buffers | OK |
| `absl::flat_hash_map<K, V>` | preferred when a map is needed | OK |
| Fixed C array `T[N]` | preferred when N is compile-time | OK |
| `cxet::Span<T>` | preferred for input/output window | OK |

If a hot-path function allocates, it must do so once, up-front, into a
caller-provided buffer. Allocation per event or per block is a bug.

Reference pattern from CXETCPP:

```cpp
// entry points take buffers in
bool encodeBlock(cxet::Span<const TradePublic> in,
                 absl::InlinedVector<uint8_t, 256 * 1024>& out);
```

---

## 4. Alignment and cache

- Structs used in SPSC rings or read on the hot path by multiple threads: `alignas(64)` (cache line) — mirrors CXETCPP composite types (`TradePublic`, `BookTickerData`, `OrderBookSnapshot`).
- Per-context arrays (AC `count0`, `count1`, rANS `freq`, `cdf`, `rcp`): declare as `alignas(64) uint16_t count0[CTX_SIZE];` so each array begins on a cache line — avoids false sharing when two codec threads touch adjacent contexts.
- Pad SPSC queue structs to cache-line boundary between producer and consumer halves.

---

## 5. JSON, parsing, networking — forbidden here

The recorder never parses exchange wire formats directly. Everything comes out
of CXETCPP as a typed C++ struct (`TradePublic`, `BookTickerData`, etc.).

Banned for this app:

- `simdjson` — it's an implementation detail of `cxet_lib.so`. Do not link.
- `rapidjson`, `nlohmann::json`, `boost::property_tree` — not needed.
- `curl`, `cpp-httplib`, `uWebSockets`, `websocketpp`, `libwebsockets` — banned. All REST/WS calls go through `runGetByConfig*` / `runSubscribe*` from CXETCPP.
- Direct `#include` of `"network/ws/WsClient.hpp"`, `"parse/..."`, `"exchanges/..."`, `"runtime/..."` — banned. See [BUILD_AND_ISOLATION.md](BUILD_AND_ISOLATION.md) for the allowed include surface.

---

## 6. I/O and logging in hot path

- **No `printf`, `std::cout`, `std::cerr`** in `src/core/` or `src/variants/`. Use `spdlog` (from `extra/spdlog`) via the thin wrapper introduced in [LOGGING_AND_METRICS.md](LOGGING_AND_METRICS.md).
- `spdlog` in hot path: only at `warn` and above. `info` may be called on block-flush boundaries, never per-event. `debug` must be compiled out in Release.
- `fmt::format` (from `extra/fmtlib`, re-used by `spdlog`) is the only formatting library.
- Writes to `.cxrec` files: **always `pwrite`** with an explicit offset. No `fwrite`. No `std::ofstream` in the writer thread. Rationale: `pwrite` gives us atomic-in-the-posix-sense block writes and lets us patch the file header at close without seeking.

---

## 7. Threads, atomics, locks

- One thread has one job. See the thread diagram in [ARCHITECTURE.md](ARCHITECTURE.md).
- **CPU pinning mandatory** for all long-running threads (producer and writer). Use a local helper `src/core/common/ThreadAffinity.hpp` that wraps `sched_setaffinity`. The CXETCPP public header does not expose its internal pinning helper, so a tiny duplicate here is allowed.
- **SPSC rings between producer and writer**: use a lock-free `SpscRing<T, Capacity>` from `src/core/common/`. Do not share mpsc or mpmc queues across stream boundaries.
- `std::atomic` with explicit `std::memory_order_*`. **No implicit `seq_cst`**. A writer draining a ring uses `acquire` on head/tail; a producer enqueueing uses `release`. The helpers in `SpscRing` enforce the pattern.
- No `std::mutex` in the hot path. Mutex is allowed only in setup/teardown and in the kline-style cold queue.
- No `std::shared_ptr` in the hot path. Ownership in `src/core/` is through raw owning pointers or `std::unique_ptr` held by a single owner.

---

## 8. Naming

Match the parent `CXETCPP` convention:

- **Types**: `camelCase` with leading uppercase — `BlockEncoder`, `StreamRecorder`, `DeltaVarIntEncoder`.
- **Functions, variables**: `camelCase` with leading lowercase — `encodeBlock`, `lastPrice`, `bufferedCount`.
- **File names**: `snake_case` — `block_encoder.hpp`, `stream_recorder.cpp`, `delta_varint_encoder.hpp`. This mirrors `src/src/exchanges/binance/fapi/` in the library.
- **Enum values**: `kAllCaps` for `enum class` entries with a domain prefix — `CodecId::Varint`, `BlockType::CoderReset`. See [API_CONTRACTS.md](API_CONTRACTS.md).
- **Compile-time constants**: `kCamelCaseOrAllCaps` — `kMaxEventsPerBlock`, `kRansM`.
- **Macros**: `HFTREC_ALL_CAPS` if unavoidable. Prefer `constexpr`/`inline constexpr`.

Variant directory names use the research naming convention from
[SOURCE_LAYOUT_AND_VARIANTS.md](SOURCE_LAYOUT_AND_VARIANTS.md):
`var02_delta_varint_cpp`, etc. Inside the directory, types use project-standard
camelCase (not `snake_case` class names).

---

## 9. Comments

- **English first**, Russian allowed after — same as CXETCPP.
- Comment **why**, not what. Function names already say what.
- Every `#pragma pack(1)` struct used as a wire format: a one-line comment stating the on-disk offsets match [FILE_FORMAT.md](FILE_FORMAT.md) byte-by-byte.
- Every `memory_order` other than `seq_cst`: one-line comment naming the pair (e.g. `// acquire — pairs with release in push()`).
- No `TODO`s without an owner and a ticket ref. Prefer opening a tracked item over leaving a `TODO`.

---

## 10. Includes

Order, no blank lines inside a group, one blank line between groups:

```cpp
// 1. The header this .cpp implements (if any)
#include "block_encoder.hpp"

// 2. App-local project includes — quoted, relative to apps/hft-recorder/src/
#include "core/common/span.hpp"
#include "core/codec/varint.hpp"

// 3. CXETCPP public includes — quoted, from the installed include dir
#include "cxet.hpp"
#include "api/stream/CxetStream.hpp"
#include "primitives/composite/Trade.hpp"

// 4. Third-party vendored (extra/) — quoted
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "spdlog/spdlog.h"

// 5. Standard library — angle brackets
#include <array>
#include <cstdint>
#include <span>
```

No `using namespace cxet;` in headers. `.cpp` files may `using` within a
function or translation-unit scope.

No circular includes. Break cycles with forward declarations.

---

## 11. Error handling

- Library code returns `bool` or a typed result struct. No exceptions thrown from `src/core/`.
- App-layer code in `src/app/` may throw on unrecoverable startup errors (bad CLI, missing `.env`, `libcxet_lib.so` not found). `main` catches and exits with a specific code — see [CONFIG_AND_CLI.md](CONFIG_AND_CLI.md).
- For per-event failures (malformed event from CXETCPP, SPSC overflow, CRC mismatch on decode), log at `warn` and emit a `GAP_MARKER` block — see [ERROR_HANDLING_AND_GAPS.md](ERROR_HANDLING_AND_GAPS.md) for the full table.
- `assert` is allowed in `Debug`, compiled out in `Release`. It is a tool for catching programmer bugs, not for validating runtime input.

---

## 12. Constants, magic numbers

Every magic number gets a named `constexpr`:

```cpp
inline constexpr uint32_t kFileMagic        = 0x43525843;     // "CXRC" LE
inline constexpr uint16_t kFormatVersion    = 0x0001;
inline constexpr uint32_t kBlockMagic       = 0x004B4C42;     // "BLK\0" LE
inline constexpr uint32_t kMaxEventsPerBlock     = 512;
inline constexpr std::chrono::seconds kMaxBlockWallTime{1};
inline constexpr uint32_t kMaxBlockBytes    = 256 * 1024;
inline constexpr uint32_t kCoderResetEveryBlocks = 1024;
inline constexpr uint32_t kRansM            = 1u << 14;      // 16384
inline constexpr uint32_t kAcMaxCount       = 4096;
```

All of these live in `src/core/common/constants.hpp` and are the single source
of truth. FILE_FORMAT.md and the constants file must agree; whenever one
changes, the other changes in the same commit.

---

## 13. Not-banned but-discouraged

- Deep inheritance hierarchies. One level of abstract base (`IBlockEncoder`, `IBlockDecoder`) is fine; anything deeper is a smell — variants should compose, not inherit.
- `std::variant` — fine for small closed sums at config time. Do not use inside encode/decode loops (pattern-matching has a vtable cost).
- Templates with heavy instantiation surface. The `CxetStream<T>` pattern is the right size — one template parameter, small N of instantiations. Variadic template gymnastics are discouraged.
- Reflection / metaprogramming beyond `if constexpr` and basic traits.

---

## 14. Code review checklist

Before submitting a PR into `apps/hft-recorder/`:

- [ ] No `double`/`float` for market values
- [ ] No `std::vector`/`std::unordered_map`/`std::string` on the hot path
- [ ] No `printf`/`cout`/`cerr`
- [ ] No direct include of CXETCPP internals (`network/`, `parse/`, `exchanges/`, `runtime/`)
- [ ] All atomics have explicit memory order
- [ ] Threads are CPU-pinned and `noexcept` on their entry lambda
- [ ] Magic numbers named in `constants.hpp`
- [ ] English-first comments
- [ ] File names `snake_case`, types `CamelCase`, funcs `camelCase`
- [ ] Includes in the 5-group order above
- [ ] Round-trip test updated if the wire format changed (see [TESTING_CONTRACT.md](TESTING_CONTRACT.md))

---

## References

- [BUILD_AND_ISOLATION.md](BUILD_AND_ISOLATION.md) — allowed include surface, CMake contract
- [API_CONTRACTS.md](API_CONTRACTS.md) — required interfaces and their signatures
- [CXETCPP_USAGE_EXAMPLES.md](CXETCPP_USAGE_EXAMPLES.md) — concrete calls to the library
- [LOGGING_AND_METRICS.md](LOGGING_AND_METRICS.md) — spdlog/Prometheus conventions
- [TESTING_CONTRACT.md](TESTING_CONTRACT.md) — what CI runs
