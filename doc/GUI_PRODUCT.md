# hft-recorder - GUI product

## Stack

The GUI stack is:
- `Qt 6`
- `C++ backend`
- `QML frontend`

The GUI is a first-class deliverable.

## Main pages

### Capture

Controls:
- symbols
- output directory
- start/stop per channel

Current supported runtime scope:
- exchange is effectively fixed to `Binance`
- market is effectively fixed to `futures_usd`
- one symbol per `CaptureCoordinator`
- multi-symbol capture is implemented as a batch of single-symbol coordinators

Live status:
- session id
- event counters per channel
- warnings/errors

Planned-but-not-current UI copy to treat carefully:
- generic exchange/market selection
- bytes written
- fully generic source coverage

### Sessions

Shows:
- all sessions on disk
- manifest summary
- quick stats
- open in validation
- open in lab

### Validation

Shows:
- trades chart
- best bid / best ask
- spread
- orderbook replay view
- raw vs decoded comparison
- mismatch counters

### Compression Lab

Shows:
- selected session
- enabled pipelines
- run progress
- metrics table
- per-stream winners

### Dashboard

Shows:
- ratio bar chart
- encode speed vs ratio
- decode speed vs ratio
- top-5 ranking cards
- export to CSV / JSON

## UI principle

The UI should feel intentional and coursework-ready:
- dark or light is allowed, but the charts must be high-contrast and readable
- not a generic admin table wall
- results must be easy to screenshot and present

## Backend rule

QML does presentation only.

All business logic lives in C++:
- capture
- corpus loading
- validation
- pipeline execution
- ranking
