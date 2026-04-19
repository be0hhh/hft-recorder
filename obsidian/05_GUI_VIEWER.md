# GUI And Viewer

Owns:

- Qt application bootstrap.
- QML screens.
- Viewmodels for capture and session lists.
- Chart viewport state.
- CPU/GPU drawing adapters for trades, ticker, and orderbook.

Main objects:

- `src/gui/app/main.cpp`
- `AppViewModel`
- `CaptureViewModel`
- `SessionListModel`
- `ChartController`
- `ChartItem`
- `ChartGpuItem`
- `viewer/renderers/*`

Actual GUI split:

- QML owns layout and user interactions.
- Viewmodels/controllers own state and backend calls.
- Core owns file I/O, capture, replay, and validation logic.

Most important runtime paths:

- `CaptureView.qml -> CaptureViewModel -> CaptureCoordinator`
- `SessionsView.qml -> SessionListModel -> recordings/`
- `ViewerView.qml -> ChartController -> SessionReplay -> RenderSnapshot -> renderers`

Depends on:

- Qt 6 QML stack
- `hftrec_core`
- `hftrec_support`

Used by:

- desktop GUI users
- agents debugging UI to backend flow

Important current facts:

- `CaptureViewModel` is more operationally important than `AppViewModel`.
- `ChartController` is the viewer brain; chart items are render surfaces, not data sources.
- GPU and CPU chart paths coexist.

If you debug a chart issue:

1. read `ChartController.cpp`
2. read `ChartGpuItem.cpp` or `ChartItem.cpp`
3. read the specific renderer in `viewer/renderers/`
4. verify replay state in `SessionReplay`

