# hft-recorder REST Marker API Guide

This file is only about the REST marker API.

REST marker API is a simple debug/offline integration for drawing vertical lines on the currently opened chart. It is useful for Java tests, Python scripts, Kafka bridges, and manual checks.

REST marker API is not the CXETCPP path. Algorithms that are supposed to behave like real trading algorithms must not use this API. They should use CXETCPP and the local venue WebSocket described in `RUN_LOCAL_VENUE_WS_GUIDE.md`.

## What REST Does

REST marker API receives a timestamp and optional label, then asks the active `ChartController` to draw a yellow vertical line.

Flow:

```text
external script/service -> HTTP POST -> hft-recorder GUI -> active chart vertical marker
```

REST does not submit an order.
REST does not go through CXETCPP.
REST does not simulate an exchange.
REST does not produce execution events.

It only draws markers on the chart.

## Standard Ports

| Service | Port | URL |
| --- | ---: | --- |
| hft-recorder REST marker API | 18080 | http://127.0.0.1:18080 |
| hft-recorder metrics | 8080 | http://127.0.0.1:8080/metrics |
| Grafana | 3000 | http://127.0.0.1:3000 |
| Prometheus | 9090 | http://127.0.0.1:9090 |

REST marker API defaults:

```bash
HFTREC_API_HOST=127.0.0.1
HFTREC_API_PORT=18080
```

Disable REST marker API:

```bash
HFTREC_API_MODE=off
```

## Build And Start

From WSL:

```bash
cd '/mnt/c/Users/be0h/PycharmProjects/course project/CXETCPP/apps/hft-recorder'
./compile.sh
HFTREC_METRICS_PORT=8080 \
HFTREC_API_HOST=127.0.0.1 HFTREC_API_PORT=18080 \
./build/start
```

Offline viewer build without CXETCPP:

```bash
cmake -B build-offline -DHFTREC_CXET_MODE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-offline -j4
HFTREC_API_HOST=127.0.0.1 HFTREC_API_PORT=18080 HFTREC_METRICS_PORT=8080 ./build-offline/bin/hft-recorder-gui
```

Offline mode is enough for REST markers. Capture will be inactive, but Viewer can load old `trades.jsonl` files.

## Open A Chart First

REST markers are drawn only on the active loaded chart.

Open `Viewer` in the GUI, then load a session folder:

```text
C:\Users\be0h\PycharmProjects\course project\CXETCPP\apps\hft-recorder\recordings\1776994215_binance_futures_usd_RAVEUSDT
```

or load only trades:

```text
C:\Users\be0h\PycharmProjects\course project\CXETCPP\apps\hft-recorder\recordings\1776994215_binance_futures_usd_RAVEUSDT\trades.jsonl
```

If no chart is loaded, API calls return `no_active_chart` or `loaded:false`.

## Health Check

PowerShell:

```powershell
Invoke-RestMethod http://127.0.0.1:18080/api/v1/health
```

Expected response when GUI found the chart controller and a chart is loaded:

```json
{"ok":true,"active_chart":true,"loaded":true}
```

Meaning:

| Field | Meaning |
| --- | --- |
| `ok` | REST server is responding |
| `active_chart` | GUI has an active `ChartController` |
| `loaded` | Viewer currently has loaded data |

If `loaded:false`, open a recording in Viewer first.

## Draw A Vertical Line

Endpoint:

```text
POST http://127.0.0.1:18080/api/v1/chart/markers/vertical
Content-Type: application/json
```

Body:

```json
{"ts_ns":"1776994215000000000","label":"signal A"}
```

Fields:

| Field | Required | Type | Meaning |
| --- | --- | --- | --- |
| `ts_ns` | yes | string or int64 | Timestamp in nanoseconds |
| `label` | no | string | Text near marker |

Use string for `ts_ns` when possible. JavaScript/JSON tooling can lose precision when a large int64 is handled as a double.

PowerShell example:

```powershell
$body = @{ ts_ns = "1776994215000000000"; label = "signal A" } | ConvertTo-Json -Compress
Invoke-RestMethod -Method Post `
  -Uri http://127.0.0.1:18080/api/v1/chart/markers/vertical `
  -ContentType 'application/json' `
  -Body $body
```

curl from WSL:

```bash
curl -X POST http://127.0.0.1:18080/api/v1/chart/markers/vertical \
  -H 'Content-Type: application/json' \
  --data '{"ts_ns":"1776994215000000000","label":"signal A"}'
```

Expected response:

```json
{"ok":true}
```

## Clear Markers

Endpoint:

```text
DELETE http://127.0.0.1:18080/api/v1/chart/markers
```

PowerShell:

```powershell
Invoke-RestMethod -Method Delete http://127.0.0.1:18080/api/v1/chart/markers
```

Expected response:

```json
{"ok":true}
```

## Java Example

Minimal Java 11+ example using `HttpClient`:

```java
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;

public class MarkerTest {
    public static void main(String[] args) throws Exception {
        String json = "{\"ts_ns\":\"1776994215000000000\",\"label\":\"java signal\"}";

        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create("http://127.0.0.1:18080/api/v1/chart/markers/vertical"))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(json))
            .build();

        HttpResponse<String> response = HttpClient.newHttpClient()
            .send(request, HttpResponse.BodyHandlers.ofString());

        System.out.println(response.statusCode());
        System.out.println(response.body());
    }
}
```

## Python Example

```python
import requests

requests.post(
    "http://127.0.0.1:18080/api/v1/chart/markers/vertical",
    json={"ts_ns": "1776994215000000000", "label": "python signal"},
    timeout=1.0,
).raise_for_status()
```

## Kafka Bridge Example

hft-recorder does not include a Kafka consumer here. Use a small bridge service:

```text
Kafka topic -> bridge process -> REST marker API -> chart marker
```

Recommended Kafka message:

```json
{"ts_ns":"1776994215000000000","label":"rave breakout"}
```

Python bridge:

```python
import json
import requests
from confluent_kafka import Consumer

consumer = Consumer({
    "bootstrap.servers": "127.0.0.1:9092",
    "group.id": "hft-recorder-marker-bridge",
    "auto.offset.reset": "latest",
})

consumer.subscribe(["hft.signals"])
api_url = "http://127.0.0.1:18080/api/v1/chart/markers/vertical"

try:
    while True:
        msg = consumer.poll(1.0)
        if msg is None:
            continue
        if msg.error():
            print("kafka error:", msg.error())
            continue

        event = json.loads(msg.value().decode("utf-8"))
        payload = {
            "ts_ns": str(event["ts_ns"]),
            "label": str(event.get("label", "signal")),
        }
        response = requests.post(api_url, json=payload, timeout=1.0)
        if response.status_code != 200:
            print("marker api error:", response.status_code, response.text)
finally:
    consumer.close()
```

If bridge runs inside Docker Desktop on Windows and GUI runs on host/WSL, use:

```text
http://host.docker.internal:18080/api/v1/chart/markers/vertical
```

In that case start GUI REST API on an address visible outside localhost:

```bash
HFTREC_API_HOST=0.0.0.0 HFTREC_API_PORT=18080 ./build/start
```

For local development, prefer `127.0.0.1`.

## Diagnostics

Check ports:

```powershell
netstat -ano | Select-String -Pattern ':18080|:8080|:3000|:9090'
```

Check Docker services:

```powershell
docker ps --format "table {{.Names}}\t{{.Ports}}\t{{.Status}}"
```

Check hft-recorder metrics:

```powershell
Invoke-WebRequest -UseBasicParsing http://127.0.0.1:8080/metrics
```

Check REST health:

```powershell
Invoke-RestMethod http://127.0.0.1:18080/api/v1/health
```

## Common Errors

| Error | Meaning | Fix |
| --- | --- | --- |
| `no_active_chart` | No loaded active chart | Open Viewer and load `trades.jsonl` or a session folder |
| `missing_ts_ns` | Body has no valid `ts_ns` | Send `ts_ns` as string nanoseconds |
| `invalid_json` | Body is not JSON object | Set `Content-Type: application/json` and send valid JSON |
| `invalid_marker` | Timestamp is invalid or chart rejected marker | Use positive nanosecond timestamp |
| API returns `ok:true`, line not visible | Timestamp is outside current viewport | Zoom/pan to marker timestamp or check ns/ms units |

## When To Use REST

Use REST when:

- testing a Java/Python service quickly;
- manually marking a timestamp;
- bridging Kafka signals to a chart during research;
- running offline viewer without CXETCPP.

Do not use REST when:

- testing the real future algorithm path;
- simulating exchange execution;
- sending real order intent through CXETCPP;
- measuring CXETCPP order latency.

For that, use `RUN_LOCAL_VENUE_WS_GUIDE.md`.
