# hft-recorder — CXETCPP Usage Examples

Concrete, copy-pasteable snippets for every CXETCPP public entrypoint used by `hft-recorder`.
Patterns come from `apps/arbitrage-screener/src/data/DataManager.cpp` (proven production pattern).

All snippets assume:

```cpp
#include "cxet.hpp"               // ONLY public CXETCPP header — never parse/ network/ runtime/
using namespace cxet;
using namespace cxet::api;
using namespace cxet::composite;
```

Boundary rule for `hft-recorder`:

- these examples define how the app calls `CXETCPP`
- they do not define the recorder's durable corpus contract
- recorder code should convert `CXETCPP` callback payloads into recorder-owned
  normalized capture rows before writing canonical JSON
- future replay/backtest consumers should depend on the recorder corpus
  contract, not on live `CXETCPP` callback shapes

> Rule: `#include "cxet.hpp"` is the **single** entry into the library. Any other include
> (`"parse/..."`, `"network/..."`, `"runtime/..."`, `"exchanges/..."`) is a rules violation —
> see `CODING_STYLE.md` § "No internal includes".

---

## 1. One-time initialisation

```cpp
int main(int argc, char** argv) {
    cxet::initBuildDispatch();        // WHY: registers all exchange dispatch tables.
                                      // Must be called EXACTLY once before any other cxet:: call.
    // ... launch threads, etc.
}
```

> Rule: `initBuildDispatch()` must run **before** any producer thread builds a
> `UnifiedRequestBuilder`. If omitted, every dispatch lookup returns "no config" and
> every `runSubscribe*` call returns `false`.

---

## 2. aggTrade stream — `CxetStream<TradePublic>`

Producer thread (pinned to `CPU_PROD_TRADES`, default `2`):

```cpp
void runTradeProducer(Symbol sym, canon::ExchangeId ex, canon::MarketType mkt,
                      SpscRing<TradePublic, kProducerRingCapacity>& ring,
                      std::atomic<bool>& stop) noexcept {
    // Build subscribe descriptor.
    UnifiedRequestBuilder b;
    b.subscribe()
     .object(cxet::composite::out::SubscribeObject::Trades)
     .exchange(ex)
     .market(mkt)
     .symbol(sym);

    // Request every field (leave aliases unset = all fields).
    // hft-recorder stores full TradePublic, unlike the screener which asks for price/ts/symbol only.

    auto stream = std::make_unique<CxetStream<TradePublic, 2048>>(b);
    stream->start();

    TradePublic ev{};
    while (!stop.load(std::memory_order_relaxed)) {
        if (stream->tryPop(ev)) {
            // Push into SPSC ring to writer thread. tryPush=false → drop + counter.
            if (!ring.tryPush(ev)) {
                metrics::eventsDropped(StreamType::AggTrade, DropReason::SpscFull).increment();
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    stream->stop();    // Idempotent; safe after signal.
}
```

Key points:

- `CxetStream<T, Capacity>` owns the WS connection. Capacity `2048` = producer-internal ring.
- `tryPop(out) → bool` — non-blocking; returns `false` when no event available.
- Sleep `200 μs` on empty (same value as arbitrage-screener). **Do not** busy-spin: other
  cores need the cycles.
- `stream->stop()` is **blocking** until WS loop drains; call only from the producer thread.

---

## 3. bookTicker stream — `CxetStream<BookTickerData>`

Identical shape, different `object`:

```cpp
UnifiedRequestBuilder b;
b.subscribe()
 .object(cxet::composite::out::SubscribeObject::BookTicker)
 .exchange(ex).market(mkt).symbol(sym);

auto stream = std::make_unique<CxetStream<BookTickerData, 2048>>(b);
stream->start();
BookTickerData ev{};
while (!stop.load(std::memory_order_relaxed)) {
    if (stream->tryPop(ev)) { /* push to writer ring */ }
    else std::this_thread::sleep_for(std::chrono::microseconds(200));
}
stream->stop();
```

> WHY `CxetStream` over `runSubscribeBookTickerByConfig`: the library exposes both.
> `CxetStream` owns an internal SPSC ring → less callback context, cleaner shutdown,
> matches the aggTrade producer pattern. The callback form is only useful for
> orderbook deltas (next section) because the library does not provide a
> `CxetStream<OrderBookSnapshot>` specialization.

---

## 4. depth@0ms deltas — `runSubscribeOrderBookDeltaByConfig` (callback)

`OrderBookDeltaOnUpdate` signature (from `api/run/RunByConfig.hpp:240`):

```cpp
using OrderBookDeltaOnUpdate =
    bool (*)(const composite::OrderBookSnapshot& delta, void* userData) noexcept;
```

The callback is invoked on the **library's WS thread**; keep it short and lock-free.
Push into SPSC, return `true` to keep going, `false` to stop.

```cpp
struct DepthCallbackCtx {
    SpscRing<OrderBookSnapshot, kProducerRingCapacity>* ring;
    std::atomic<bool>*                                  stopFlag;
};

static bool onDepthDelta(const composite::OrderBookSnapshot& delta, void* userData) noexcept {
    auto* ctx = static_cast<DepthCallbackCtx*>(userData);
    if (ctx->stopFlag->load(std::memory_order_relaxed)) return false;    // request stop
    if (!ctx->ring->tryPush(delta)) {
        metrics::eventsDropped(StreamType::DepthUpdate, DropReason::SpscFull).increment();
        // Keep going; dropped event → GAP_MARKER on next flush (see ERROR_HANDLING_AND_GAPS.md).
    }
    return true;
}

void runDepthProducer(Symbol sym, canon::ExchangeId ex, canon::MarketType mkt,
                      SpscRing<OrderBookSnapshot, kProducerRingCapacity>& ring,
                      std::atomic<bool>& stop) noexcept {
    UnifiedRequestBuilder b;
    b.subscribe()
     .object(cxet::composite::out::SubscribeObject::OrderBook)    // depth@0ms for fapi
     .exchange(ex).market(mkt).symbol(sym);

    MessageBuffer payloadBuf{};        // 64 KB each — on producer stack is fine, not hot path.
    MessageBuffer recvBuf{};

    DepthCallbackCtx ctx{&ring, &stop};

    const bool ok = runSubscribeOrderBookDeltaByConfig(
        b, payloadBuf, recvBuf,
        &onDepthDelta, &ctx,
        /* maxUpdates        */ 0,        // 0 = unlimited
        /* stopRequested     */ &stop,
        /* maxReconnectAttempts */ 8,     // auto-reconnect with backoff
        /* pingIntervalMs    */ 20'000);  // 20 s keepalive for Binance fapi
    if (!ok) {
        logError("depth@0ms subscribe failed for {}", sym.cStr());
    }
}
```

Notes:

- `runSubscribeOrderBookDeltaByConfig` **blocks** the calling thread until the WS loop exits
  (either `maxUpdates` reached, callback returned `false`, or `stopRequested == true`).
- Do the block on a dedicated producer thread (`CPU_PROD_DEPTH`, default `4`).
- `MessageBuffer` is `64 KB`; two of them on the stack is fine outside the hot path.
- `ping_interval_ms = 20'000` matches Binance fapi server-side expectation.
- Reconnect: library re-fetches snapshot internally on gap detection. Our recorder emits
  `CODER_RESET` + `GAP_MARKER` on the next flush — see `ERROR_HANDLING_AND_GAPS.md`.

---

## 5. Orderbook snapshot — `runGetOrderBookByConfig` (REST poll)

The snapshot is a **one-shot REST fetch**, not a stream. Dedicated thread sleeps 60 s between
calls (Binance recommended cadence for full depth refresh).

```cpp
void runSnapshotPoller(Symbol sym, canon::ExchangeId ex, canon::MarketType mkt,
                       SpscRing<OrderBookSnapshot, 4>& ring,
                       std::atomic<bool>& stop) noexcept {
    MessageBuffer reqBuf{};
    MessageBuffer recvBuf{};
    OrderBookSnapshot snap{};

    while (!stop.load(std::memory_order_relaxed)) {
        UnifiedRequestBuilder b;
        b.get()
         .object(cxet::composite::out::GetObject::OrderBook)
         .exchange(ex).market(mkt).symbol(sym);

        const bool ok = runGetOrderBookByConfig(b, reqBuf, recvBuf, &snap);
        if (ok) {
            (void)ring.tryPush(snap);       // writer reads, persists one block per snapshot.
        } else {
            logWarn("snapshot fetch failed for {}; retry in 60s", sym.cStr());
        }

        // Interruptible sleep: 600 × 100 ms = 60 s.
        for (int i = 0; i < 600 && !stop.load(std::memory_order_relaxed); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
```

Why not a single `sleep_for(60s)`: SIGTERM must exit within ~100 ms; a 60-second sleep
masks shutdown.

---

## 6. CPU pinning — via CXETCPP helper

```cpp
// Producer thread entry:
const bool pinned = cxet::os::setThisThreadAffinity(/* cpuId = */ 2);
if (!pinned) logWarn("affinity set failed for trade producer (CPU 2)");

// ... then runTradeProducer(...);
```

If `cxet::os::setThisThreadAffinity` is not exposed by the installed CXETCPP version,
fall back to a tiny local helper in `src/core/os/Affinity.hpp` that wraps
`pthread_setaffinity_np` (Linux only — we build with `-DCXET_LINUX_ONLY`).

CPU map (from `CONFIG_AND_CLI.md`):

| Thread | CPU |
|---|---|
| main | 0 |
| producer trades | 2 |
| producer bookTicker | 3 |
| producer depth@0ms | 4 |
| producer snapshot | 5 |
| control (SIGTERM/metrics) | 7 |
| writer trades | 8 |
| writer bookTicker | 9 |
| writer depth@0ms | 10 |
| writer snapshot | 11 |

---

## 7. MessageBuffer — who owns what

CXETCPP exposes `MessageBuffer` as a 64 KB owned buffer. For recorder:

- **Producer thread**: one `MessageBuffer payloadBuf` + one `MessageBuffer recvBuf` per
  `runSubscribe…` / `runGet…` call, declared as local automatic storage. This is NOT the hot
  path — allocation happens once at thread start.
- **CxetStream<T>**: manages its own internal buffers; do not pass one in.

> Do **not** use `MessageBufferPool` in hft-recorder. The pool exists for multi-symbol
> multiplexers (screeners). We have one connection per producer thread, so each thread
> owns its two buffers directly — simpler lifecycles, zero lock contention.

---

## 8. FORBIDDEN — what not to do

```cpp
// ❌ Include internal headers
#include "parse/...".        // rules violation
#include "network/ws/...".   // ditto
#include "runtime/...".      // ditto

// ❌ Write your own WS/REST client
boost::beast::websocket::stream<...>  myWs;   // redundant; CxetStream handles it.

// ❌ Use double / float for prices
double price = snap.bids[0].price / 1e8;      // LOSS of precision; use Price (int64).

// ❌ Use std::string in hot path
std::string sym = "BTCUSDT";                  // alloc per tick; use Symbol.

// ❌ Throw exceptions
if (!ok) throw std::runtime_error("nope");    // library is -fno-exceptions; use Status.

// ❌ Log via printf / cout
std::cout << "trade: " << price << "\n";      // use spdlog wrapper — see LOGGING_AND_METRICS.md.

// ❌ Call initBuildDispatch() more than once
cxet::initBuildDispatch();    // in main()
cxet::initBuildDispatch();    // ❌ in some other constructor — undefined state.

// ❌ Share a MessageBuffer between threads
static MessageBuffer gBuf{};                  // data-race; each thread owns its own.
```

---

## References

- `apps/arbitrage-screener/src/data/DataManager.cpp` — reference pattern for `CxetStream<T>` +
  producer/poll thread (lines 148–271 for batched+markprice streams).
- `CXETCPP/src/src/api/run/RunByConfig.hpp:247-255` — `runSubscribeOrderBookDeltaByConfig` signature.
- `CXETCPP/src/src/api/run/RunByConfig.hpp:168-171` — `runGetOrderBookByConfig` signature.
- `CODING_STYLE.md` — primitive types, container bans, logging rules.
- `API_CONTRACTS.md` — internal `SpscRing<T>`, `IStreamRecorder`, `BlockWriter` interfaces.
- `ERROR_HANDLING_AND_GAPS.md` — what happens when `tryPush` fails or WS drops.
- `CONFIG_AND_CLI.md` — CPU pin assignments, `.env` schema.
