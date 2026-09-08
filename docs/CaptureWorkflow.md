# hft-recorder - capture workflow

## User workflow

1. User opens the GUI.
2. User chooses exchange, market, symbols, duration, and output directory.
3. User presses `Start Capture`.
4. The app creates a session directory and manifest.
5. Channel writers begin writing canonical JSON files.
6. The app updates live counters and mini-charts.
7. User presses `Stop` or the duration elapses.
8. Writers flush, finalize, and manifest closes cleanly.

## First practical scope

First delivery target:
- `Binance FAPI`
- multi-symbol architecture
- first polished demo may still be one symbol

## Error handling

Errors should be surfaced in:
- GUI status line
- session manifest
- log files

Capture should fail loudly on:
- session directory creation failure
- channel file open failure
- malformed config

Capture may continue with warnings on:
- one snapshot refresh failure
- non-fatal reconnects
