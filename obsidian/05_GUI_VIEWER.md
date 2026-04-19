# GUI And Viewer

Owns:

- Qt application bootstrap.
- QML screens.
- Viewmodels for capture and session lists.
- Chart viewport state.
- chart rendering and viewer interactions.

Main objects:

- `src/gui/app/main.cpp`
- `AppViewModel`
- `CaptureViewModel`
- `SessionListModel`
- `ChartController`
- `ChartItem`
- `viewer/renderers/*`

Actual GUI split:

- QML owns layout and user interactions.
- Viewmodels/controllers own state and backend calls.
- Core owns file I/O, capture, replay, and validation logic.

Most important runtime paths:

- `CaptureView.qml -> CaptureViewModel -> CaptureCoordinator`
- `ViewerView.qml -> ChartController -> SessionReplay -> RenderSnapshot -> renderers`
- `ViewerView.qml -> AppViewModel -> QSettings`

Depends on:

- Qt 6 QML stack
- `hftrec_core`
- `hftrec_support`

Used by:

- desktop GUI users
- agents debugging replay, rendering, and comparison workflow

Important current facts:

- `ChartController` is the viewer brain; chart items are render surfaces, not data sources.
- `ViewerView` already supports `Shift + LMB` rectangle selection and a compact overlay summary.
- top viewer controls are persistent through `AppViewModel -> QSettings`.
- orderbook controls are dollar-based:
  - `Full Bright @` = dollar level that reaches full line intensity
  - `Min Visible` = dollar filter that removes weaker book levels from the renderer
- `BookTicker` stays visible even when the orderbook dollar filter hides weak levels.
- current orderbook baseline is line-based and readability-first; heavy fill passes were removed after artifact issues.
- book rendering is clipped to real session coverage, so there are no fake tails before the first event or after the last event.
- the current viewer is no longer just a playback surface; it is the baseline visual comparison workbench for later compression and reconstruction work.

If you debug a chart issue:

1. read `ChartController.cpp`
2. read `ChartItem.cpp`
3. read the specific renderer in `viewer/renderers/`
4. verify replay state in `SessionReplay`
5. verify viewer baseline assumptions in `17_VIEWER_BASELINE_2026_04`
