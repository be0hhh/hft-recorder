# File Roles

Format:

- `file -> role -> depends on -> used by`

Entrypoints:

- `src/app/main.cpp -> CLI dispatcher for capture/analyze/report -> subcommand files -> CLI users`
- `src/app/capture_cli.cpp -> legacy/support capture entry -> CaptureCoordinator -> CLI capture flow`
- `src/gui/app/main.cpp -> Qt bootstrap and QML registration -> viewmodels/controllers/qml -> GUI`

Capture:

- `src/core/capture/CaptureCoordinator.hpp/.cpp -> owns session lifecycle and channel capture threads -> cxet public API, writers, manifest -> CaptureViewModel and CLI`
- `src/core/capture/CaptureCoordinatorInternal.hpp/.cpp -> shared capture runtime helpers and supported-config rules -> CaptureCoordinator units -> active capture path`
- `src/core/capture/CaptureCoordinatorRuntime.cpp -> channel-specific CXET runtime and callback orchestration -> CaptureCoordinator facade -> active capture path`
- `src/core/capture/ChannelJsonWriter.hpp/.cpp -> appends canonical JSONL per channel -> filesystem -> CaptureCoordinator`
- `src/core/capture/JsonSerializers.hpp/.cpp -> normalized event to JSON rendering -> cxet structs -> CaptureCoordinator`
- `src/core/capture/SessionManifest.hpp/.cpp -> session metadata model and persistence -> filesystem -> CaptureCoordinator, CorpusLoader`
- `src/core/capture/SessionId.hpp/.cpp -> stable session folder naming -> time/config -> CaptureCoordinator`

Replay and corpus:

- `src/core/replay/SessionReplay.hpp/.cpp -> loads and merges session timeline -> JsonLineParser, BookState -> ChartController`
- `src/core/replay/SessionReplayTimeline.cpp -> replay cursor, event merge, and orderbook integrity policy -> SessionReplay facade -> active replay path`
- `src/core/replay/JsonLineParser.hpp/.cpp -> parses corpus JSON lines into rows -> replay row structs -> SessionReplay`
- `src/core/replay/BookState.hpp/.cpp -> mutable in-memory book reconstructed from snapshots and deltas -> replay events -> SessionReplay`
- `src/core/corpus/CorpusLoader.hpp/.cpp -> loads session folder into SessionCorpus -> filesystem -> lab/backend`
- `src/core/corpus/SessionCorpus.hpp -> in-memory canonical corpus bundle -> manifest + raw lines -> LabRunner`

GUI:

- `src/gui/viewmodels/CaptureViewModel.hpp/.cpp -> thin QML facade for capture screen state -> request/batch/state helpers -> CaptureView.qml`
- `src/gui/viewmodels/CaptureViewModelRequests.cpp -> symbol normalization, alias policy, request preview, and config synthesis -> CXET field source + CaptureConfig -> CaptureViewModel`
- `src/gui/viewmodels/CaptureViewModelBatch.cpp -> coordinator batch start/stop/finalize supervision -> CaptureCoordinator -> CaptureViewModel`
- `src/gui/viewmodels/CaptureViewModelState.cpp -> timer-driven aggregation of session/channel counters and surfaced errors -> CaptureCoordinator batch -> CaptureViewModel`
- `src/gui/models/SessionListModel.hpp/.cpp -> lists recorded sessions from disk -> filesystem -> SessionsView.qml`
- `src/gui/qml/components/Capture*.qml -> reusable capture controls/cards -> CaptureViewModel bindings -> CaptureView.qml`
- `src/gui/qml/components/Viewer*.qml -> reusable viewer toolbar/scale/selection pieces plus local interaction helper -> ChartController/AppViewModel/SessionListModel -> ViewerView.qml`
- `src/gui/qml/views/CaptureView.qml -> capture-screen composition shell -> Capture components + CaptureViewModel -> GUI capture tab`
- `src/gui/qml/views/ViewerView.qml -> viewer-screen composition shell -> Viewer components + ChartController/AppViewModel -> GUI viewer tab`
- `src/gui/viewer/ChartController.hpp/.cpp -> thin viewer facade and renderer capability detection -> replay/view helpers -> ChartItem/ChartGpuItem`
- `src/gui/viewer/ChartControllerSession.cpp -> session file loading and replay finalize/reset flow -> SessionReplay -> ChartController`
- `src/gui/viewer/ChartControllerViewport.cpp -> viewport math, scale labels, cursor sync, and render snapshot construction -> SessionReplay + RenderSnapshot -> ChartController`
- `src/gui/viewer/ChartControllerSelection.cpp -> rectangle selection mapping and summary/statistics extraction -> SessionReplay book/trade state -> ChartController`
- `src/gui/viewer/ChartItem.cpp -> thin painted-item facade and property/setter wiring -> ChartItem hover/paint helpers -> ViewerView.qml`
- `src/gui/viewer/ChartItemHover.cpp -> hover/context hit orchestration and stable hovered payload state -> HoverDetection + cached snapshot -> ChartItem`
- `src/gui/viewer/ChartItemPaint.cpp -> snapshot cache lifecycle, geometry invalidation, and renderer call sequencing -> ChartController + renderers -> ChartItem`
- `src/gui/viewer/ChartGpuItem.cpp -> GPU-oriented drawing path -> ChartController snapshots -> ViewerView.qml`
- `src/gui/viewer/renderers/* -> draw book, ticker, trades, overlay layers -> RenderSnapshot -> chart items`

Lab and variants:

- `src/core/lab/LabRunner.hpp/.cpp -> runs pipeline descriptors over SessionCorpus -> BookFrameSampler, validation structs -> future lab and ranking flows`
- `src/core/lab/RankingEngine.hpp/.cpp -> sorts pipeline results -> PipelineResult -> lab dashboards/reports`
- `src/core/validation/ValidationRunner.hpp/.cpp -> compares original and decoded sequences -> ValidationResult -> compression validation`
- `src/variants/* -> custom codec candidates per stream family -> hftrec_core/support -> CLI bench and future lab`
- `src/support/external_wrappers/* -> wrappers around zstd/lz4/brotli/xz baselines -> hftrec_core -> CLI bench and future lab`

Scaffold or low-signal areas:

- `src/core/cxet_bridge/CxetCaptureBridge.cpp -> CXET-to-recorder row mapping helpers used by capture runtime -> CXET public composite/runtime types -> CaptureCoordinatorRuntime and JsonSerializers`
- `src/core/stream/StreamRecorder.hpp -> producer interface declaration only -> dataset enums/status -> transitional`
- `src/core/block/* and src/core/codec/* -> file/block/varint scaffolding -> core internals -> not the center of current product`
