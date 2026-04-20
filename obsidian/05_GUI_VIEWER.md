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
- Large QML screens are now decomposed into `src/gui/qml/components/*`; top-level views act as composition shells.

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

- `ChartController` remains the viewer facade, but the heavy logic is split:
  - `ChartControllerSession.cpp` owns load/reset/finalize file flows
  - `ChartControllerViewport.cpp` owns viewport math and snapshot generation
  - `ChartControllerSelection.cpp` owns rectangle selection and derived summary stats
- `ChartItem` is no longer one mixed runtime blob:
  - `ChartItem.cpp` keeps property/setter wiring
  - `ChartItemHover.cpp` owns hover/context hit recomputation
  - `ChartItemPaint.cpp` owns snapshot cache invalidation and painter orchestration
- `ViewerView.qml` is no longer the only place that owns toolbar/scale/selection UI:
  - `ViewerSessionToolbar.qml` owns session picker chrome
  - `ViewerLayerToolbar.qml` owns layer toggles and slider controls
  - `ViewerSelectionOverlay.qml`, `ViewerPriceScale.qml`, and `ViewerTimeScale.qml` own the viewer-side support surfaces
  - `ViewerInteractionState.qml` holds local interaction/selection helper state that stays on the QML side
- `CaptureView.qml` is likewise decomposed into reusable `Capture*.qml` controls and request cards.
- chart items are render surfaces, not data sources.
- `ViewerView` already supports `Shift + LMB` rectangle selection and a compact overlay summary.
- top viewer controls are persistent through `AppViewModel -> QSettings`.
- viewer internals improved materially during the current pass:
  - viewport/session/selection logic is split more cleanly
  - scale widgets are isolated from the main composition shell
  - chart rendering and viewer chrome are easier to audit than before
- orderbook controls are dollar-based:
  - `Full Bright @` = dollar level that reaches full line intensity
  - `Min Visible` = dollar filter that removes weaker book levels from the renderer
- `BookTicker` stays visible even when the orderbook dollar filter hides weak levels.
- current orderbook baseline is line-based and readability-first; heavy fill passes were removed after artifact issues.
- book rendering is clipped to real session coverage, so there are no fake tails before the first event or after the last event.
- the current viewer is no longer just a playback surface; it is intended to be the baseline visual comparison workbench for later compression and reconstruction work.
- important live caveat:
  - recent screenshots still show price/time scales visually detached from the chart surface
  - treat axis correctness as unresolved until the live GUI matches the rendered market shape
  - do not describe the baseline as visually stable end-to-end yet

If you debug a chart issue:

1. read the relevant `ChartController*.cpp` unit for the concern:
   session / viewport / selection
2. read the relevant `ChartItem*.cpp` unit for item runtime concern:
   setters / hover / paint-cache
3. read the specific renderer in `viewer/renderers/`
4. verify replay state in `SessionReplay`
5. verify viewer baseline assumptions in `17_VIEWER_BASELINE_2026_04`
6. if the chart shape looks right but the axes look frozen / detached, verify runtime scale reactivity before changing replay math
