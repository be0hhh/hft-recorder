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
- exchange
- market
- symbols
- duration
- snapshot interval
- output directory
- start
- stop

Live status:
- session id
- elapsed time
- event counters per channel
- bytes written
- warnings/errors

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
