# hft-recorder - configuration and app entrypoints

## Main product entrypoint

The main user entrypoint is the Qt GUI application:
- `hft-recorder-gui`

CLI tools remain secondary support tools for:
- corpus inspection
- lab automation
- exports

## Environment file

The app reads a local `.env` file for defaults.

Expected keys:
- `EXCHANGE=binance`
- `MARKET=futures_usd`
- `SYMBOLS=BTCUSDT,ETHUSDT,SOLUSDT`
- `DURATION_SEC=1800`
- `OUTPUT_DIR=./recordings`
- `SNAPSHOT_INTERVAL_SEC=60`
- `LOG_LEVEL=info`
- `LOG_DIR=./logs`

Optional:
- `BINANCE_API_KEY`
- `BINANCE_SECRET`

## GUI config behavior

Rules:
- `.env` provides startup defaults
- GUI fields can override those values for the current session
- session-specific choices are written into `manifest.json`

## Secondary CLI tools

### `hft-recorder-cli`

Purpose:
- optional headless recording or corpus inspection

### `hft-recorder-lab`

Purpose:
- optional headless benchmark execution on a session

These tools are secondary to the GUI and should mirror the same backend
services, not invent a second architecture.
