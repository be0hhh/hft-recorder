# Runtime Graph

```mermaid
flowchart TD
    CLI["src/app/main.cpp"] --> CAPCLI["capture_cli.cpp"]
    CLI --> ANCLI["analyze_cli.cpp"]
    CLI --> RPTCLI["report_export.cpp"]

    GUIMAIN["src/gui/app/main.cpp"] --> QML["qml/Main.qml"]
    GUIMAIN --> AVM["AppViewModel"]
    GUIMAIN --> CVM["CaptureViewModel"]
    GUIMAIN --> SLM["SessionListModel"]
    GUIMAIN --> CC["ChartController"]
    GUIMAIN --> CGI["ChartGpuItem / ChartItem"]

    CVM --> COORD["CaptureCoordinator"]
    COORD --> WRITERS["ChannelJsonWriter + JsonSerializers"]
    COORD --> MANIFEST["SessionManifest + SessionId"]
    COORD --> CXET["prebuilt cxet_lib public API"]
    WRITERS --> SESSION["recordings/<session_id>/ JSON corpus"]
    MANIFEST --> SESSION

    SLM --> SESSION
    CC --> REPLAY["SessionReplay"]
    REPLAY --> PARSER["JsonLineParser"]
    REPLAY --> BOOK["BookState"]
    SESSION --> REPLAY
    REPLAY --> VIEWER["renderers / hover / overlays"]

    SESSION --> CORPUS["CorpusLoader -> SessionCorpus"]
    CORPUS --> LAB["LabRunner"]
    LAB --> VALID["ValidationRunner"]
    LAB --> RANK["RankingEngine"]
    LAB --> VARS["variants/*"]
    LAB --> WRAPS["support/external_wrappers/*"]
```

Owns:

- Top-level runtime flow.
- Where each entrypoint hands off next.

Depends on:

- `src/app/`
- `src/gui/`
- `src/core/`
- prebuilt `cxet_lib`

Used by:

- Any agent trying to answer "where do I start?"

Fast path:

- Capture problem: go to [[03_CAPTURE]]
- Replay or viewer problem: go to [[04_REPLAY_VALIDATION]] and [[05_GUI_VIEWER]]
- Compression or benchmarking problem: go to [[06_LAB_VARIANTS]]

