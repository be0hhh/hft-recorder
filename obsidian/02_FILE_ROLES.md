# File Roles

Format:

- `file -> role -> depends on -> used by`

Entrypoints:

- `src/app/main.cpp -> CLI dispatcher for capture/analyze/report -> subcommand files -> CLI users`
- `src/app/capture_cli.cpp -> legacy/support capture entry -> CaptureCoordinator -> CLI capture flow`
- `src/gui/app/main.cpp -> Qt bootstrap and QML registration -> viewmodels/controllers/qml -> GUI`

Capture:

- `src/core/capture/CaptureCoordinator.hpp/.cpp -> owns session lifecycle and channel capture threads -> cxet public API, writers, manifest -> CaptureViewModel and CLI`
- `src/core/capture/ChannelJsonWriter.hpp/.cpp -> appends canonical JSONL per channel -> filesystem -> CaptureCoordinator`
- `src/core/capture/JsonSerializers.hpp/.cpp -> normalized event to JSON rendering -> cxet structs -> CaptureCoordinator`
- `src/core/capture/SessionManifest.hpp/.cpp -> session metadata model and persistence -> filesystem -> CaptureCoordinator, CorpusLoader`
- `src/core/capture/SessionId.hpp/.cpp -> stable session folder naming -> time/config -> CaptureCoordinator`

Replay and corpus:

- `src/core/replay/SessionReplay.hpp/.cpp -> loads and merges session timeline -> JsonLineParser, BookState -> ChartController`
- `src/core/replay/JsonLineParser.hpp/.cpp -> parses corpus JSON lines into rows -> replay row structs -> SessionReplay`
- `src/core/replay/BookState.hpp/.cpp -> mutable in-memory book reconstructed from snapshots and deltas -> replay events -> SessionReplay`
- `src/core/corpus/CorpusLoader.hpp/.cpp -> loads session folder into SessionCorpus -> filesystem -> lab/backend`
- `src/core/corpus/SessionCorpus.hpp -> in-memory canonical corpus bundle -> manifest + raw lines -> LabRunner`

GUI:

- `src/gui/viewmodels/CaptureViewModel.hpp/.cpp -> GUI facade for multi-symbol capture -> CaptureCoordinator -> CaptureView.qml`
- `src/gui/models/SessionListModel.hpp/.cpp -> lists recorded sessions from disk -> filesystem -> SessionsView.qml`
- `src/gui/viewer/ChartController.hpp/.cpp -> viewer orchestration and viewport state -> SessionReplay -> ChartItem/ChartGpuItem`
- `src/gui/viewer/ChartGpuItem.cpp -> GPU-oriented drawing path -> ChartController snapshots -> ViewerView.qml`
- `src/gui/viewer/renderers/* -> draw book, ticker, trades, overlay layers -> RenderSnapshot -> chart items`

Lab and variants:

- `src/core/lab/LabRunner.hpp/.cpp -> runs pipeline descriptors over SessionCorpus -> BookFrameSampler, validation structs -> future lab and ranking flows`
- `src/core/lab/RankingEngine.hpp/.cpp -> sorts pipeline results -> PipelineResult -> lab dashboards/reports`
- `src/core/validation/ValidationRunner.hpp/.cpp -> compares original and decoded sequences -> ValidationResult -> compression validation`
- `src/variants/* -> custom codec candidates per stream family -> hftrec_core/support -> CLI bench and future lab`
- `src/support/external_wrappers/* -> wrappers around zstd/lz4/brotli/xz baselines -> hftrec_core -> CLI bench and future lab`

Scaffold or low-signal areas:

- `src/core/cxet_bridge/CxetCaptureBridge.cpp -> currently unimplemented bridge stub -> Status -> not central today`
- `src/core/stream/StreamRecorder.hpp -> producer interface declaration only -> dataset enums/status -> transitional`
- `src/core/block/* and src/core/codec/* -> file/block/varint scaffolding -> core internals -> not the center of current product`

