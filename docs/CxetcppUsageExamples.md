# hft-recorder — current CXET capture boundary

Status: current source integration, static-only evidence.

Recorder consumes the same CXET Core runtime as Parser and HFT Trader. Its live
capture adapter is `src/Runtime/src/Capture/MarketData/NativeMarketCapture.cpp`;
normalized row conversion is owned by `Capture/Bridge/CxetCaptureBridge.cpp`.
There is no `CxetStream`, tape callback or alternate Recorder network runtime.

## Cold configuration

`NativeMarketCapture::configure` receives exact configured sources and sinks,
selects the registered descriptor, allocates bounded event/level handoff storage
and configures `ConfiguredMarketOwner` on its `MarketRuntime` transport. Missing
route, identity, sink or budget rejects configuration. Recorder asks for logical
objects; exchange-owned CXET code selects native wire grammar.

## Native consumption

`NativeCaptureHooks` consumes canonical Trade/BBO commits and borrowed Depth
frames after Core admission. BBO presence flags and commit metadata are preserved.
Depth levels are copied into the prepared bounded row arena before the borrowed
frame can be released. Handoff overflow records loss and stops exact recording;
it never allocates a larger queue or substitutes another input path.

Core owns transport progress, ingress drain and release. Recorder's storage
worker owns row conversion, manifests and disk I/O after the bounded handoff.
Shutdown retires the configured owner and joins the storage worker before
releasing handoff storage.

## Corpus boundaries

Current direct JSON capture cannot represent an Unknown Trade initiator side or
native Depth snapshot/rebase metadata. These inputs stop that capture with an
explicit channel error/loss record; Recorder never invents a side or delta.
Supported native deltas preserve zero quantity as level deletion.

Parser binary capture is a separate producer-owned external capture product. It
preserves native Unknown side as RecordedOnly, according to the current exact
replay admission contract. Corpus schemas and serialized numeric tags retain their
fail-closed guards; they are not aliases for another live runtime.

Arrival metadata comes from the admitted application frame. REST historical
backfill stays historical and does not become a live strategy delivery. See
[BacktestEngineContract.md](BacktestEngineContract.md) and
[DeliveryTimestampsContract.md](DeliveryTimestampsContract.md) for exact corpus
admission. These source boundaries do not establish build, GUI, replay or live
provider acceptance.
