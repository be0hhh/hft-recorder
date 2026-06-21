#!/usr/bin/env python3
"""Build the hft-recorder Finam asset catalog JSON.

Run from the CXETCPP root:

    python3 apps/hft-recorder/tools/update_finam_assets.py

The script creates a fresh Finam session from FINAM_API_1_SECRET, pages
/v1/assets/all, keeps spot-like instruments and futures, and writes a compact
catalog consumed by the recorder GUI symbol picker.
"""

from __future__ import annotations

import argparse
import datetime as dt
import http.client
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[3]
RECORDER_ENV = ROOT / "apps" / "hft-recorder" / ".env"
OUT_PATH = ROOT / "apps" / "hft-recorder" / "src" / "gui" / "data" / "finam_assets.json"
CHECKPOINT_PATH = Path("/tmp/hftrec_finam_assets_checkpoint.json")
SESSION_URL = "https://api.finam.ru/v1/sessions"
ASSETS_ALL_URL = "https://api.finam.ru/v1/assets/all"

UNDERLYING_OVERRIDES = {
    "SBER": "SBRF",
    "SBERP": "SBRF",
    "GAZP": "GAZR",
    "LKOH": "LKOH",
    "SIBN": "SIBN",
    "VTBR": "VTBR",
    "ROSN": "ROSN",
    "GMKN": "GMKR",
    "NVTK": "NOTK",
    "PLZL": "PLZL",
    "MGNT": "MGNT",
    "ALRS": "ALRS",
    "AFLT": "AFLT",
}

SPOT_TYPES = {"EQUITIES", "ETF", "BOND", "CURRENCY", "OTHER"}
FUTURE_MONTHS = {
    "F": 1,
    "G": 2,
    "H": 3,
    "J": 4,
    "K": 5,
    "M": 6,
    "N": 7,
    "Q": 8,
    "U": 9,
    "V": 10,
    "X": 11,
    "Z": 12,
}


class FinamCatalogError(RuntimeError):
    pass


def read_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    if not path.exists():
        return values
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def http_json(url: str,
              *,
              payload: dict[str, Any] | None = None,
              headers: dict[str, str] | None = None,
              timeout: float = 45.0) -> dict[str, Any]:
    body = None
    method = "GET"
    request_headers = {"Accept": "application/json"}
    if headers:
        request_headers.update(headers)
    if payload is not None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        request_headers["Content-Type"] = "application/json"
        method = "POST"
    request = urllib.request.Request(url, data=body, headers=request_headers, method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")
        raise FinamCatalogError(f"Finam HTTP {exc.code} for {url}: {detail[:500]}") from exc
    except (urllib.error.URLError, TimeoutError, http.client.IncompleteRead) as exc:
        raise FinamCatalogError(f"Cannot read Finam response for {url}: {exc}") from exc
    try:
        decoded = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise FinamCatalogError(f"Finam returned non-JSON for {url}: {raw[:500]}") from exc
    if not isinstance(decoded, dict):
        raise FinamCatalogError(f"Finam returned unexpected JSON for {url}: {type(decoded).__name__}")
    return decoded


def find_token(response: dict[str, Any]) -> str:
    for key in ("token", "jwt", "access_token", "accessToken"):
        value = response.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()
    for value in response.values():
        if isinstance(value, dict):
            token = find_token(value)
            if token:
                return token
    return ""


def create_session(env: dict[str, str]) -> str:
    secret = env.get("FINAM_API_1_SECRET", "").strip()
    if not secret:
        raise FinamCatalogError(f"FINAM_API_1_SECRET is missing in {RECORDER_ENV}")
    payload: dict[str, Any] = {"secret": secret}
    source_app_id = env.get("FINAM_API_1_SOURCE_APP_ID", "").strip()
    if source_app_id:
        payload["source_app_id"] = source_app_id
    response = http_json(SESSION_URL, payload=payload, timeout=30.0)
    token = find_token(response)
    if not token:
        raise FinamCatalogError(f"Cannot find token in /v1/sessions response: {response!r}")
    return token


def read_checkpoint(path: Path) -> tuple[list[dict[str, Any]], str, int]:
    if not path.exists():
        return [], "", 0
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return [], "", 0
    assets = data.get("assets")
    cursor = str(data.get("cursor") or "").strip()
    page = int(data.get("page") or 0)
    if cursor == "0":
        cursor = ""
    if not isinstance(assets, list):
        return [], "", 0
    return [item for item in assets if isinstance(item, dict)], cursor, page


def write_checkpoint(path: Path, assets: list[dict[str, Any]], cursor: str, page: int) -> None:
    path.write_text(json.dumps({
        "page": page,
        "cursor": cursor,
        "assets": assets,
    }, ensure_ascii=False), encoding="utf-8")


def fetch_assets(env: dict[str, str],
                 *,
                 max_pages: int,
                 retry_count: int,
                 checkpoint_path: Path,
                 fresh: bool) -> tuple[list[dict[str, Any]], bool, str]:
    if fresh and checkpoint_path.exists():
        checkpoint_path.unlink()
    assets, cursor, start_page = read_checkpoint(checkpoint_path)
    if assets:
        print(f"resume checkpoint page={start_page} total={len(assets)} cursor={cursor}", flush=True)
    complete = False
    if start_page >= max_pages:
        return assets, False, cursor
    token = create_session(env)
    for page in range(start_page + 1, max_pages + 1):
        url = ASSETS_ALL_URL
        if cursor:
            url += "?cursor=" + urllib.parse.quote(cursor)
        response: dict[str, Any] | None = None
        for attempt in range(1, retry_count + 1):
            try:
                response = http_json(url,
                                     headers={"Authorization": "Bearer " + token},
                                     timeout=60.0)
                break
            except FinamCatalogError as exc:
                if "HTTP 401" in str(exc):
                    print(f"refresh token page={page} attempt={attempt}: {exc}", file=sys.stderr, flush=True)
                    token = create_session(env)
                    continue
                if attempt == retry_count:
                    raise
                print(f"retry page={page} attempt={attempt}: {exc}", file=sys.stderr, flush=True)
                time.sleep(min(2 * attempt, 10))
        assert response is not None
        batch = response.get("assets")
        if not isinstance(batch, list):
            raise FinamCatalogError(f"Finam /v1/assets/all response has no assets array at page {page}")
        assets.extend(item for item in batch if isinstance(item, dict))
        cursor_value = response.get("next_cursor")
        cursor = str(cursor_value).strip() if cursor_value is not None else ""
        if cursor == "0":
            cursor = ""
        print(f"page={page} batch={len(batch)} total={len(assets)} next={cursor}", flush=True)
        write_checkpoint(checkpoint_path, assets, cursor, page)
        if not cursor or not batch:
            complete = True
            break
    return assets, complete, cursor


def normalize_text(value: Any) -> str:
    return str(value).strip() if value is not None else ""


def futures_underlying_from_name(name: str) -> str:
    match = re.match(r"^([A-Za-z0-9]+)-", name.strip())
    return match.group(1).upper() if match else ""


def future_expiration_from_name(name: str) -> str:
    match = re.search(r"-(\d{1,2})\.(\d{2})(?:\D|$)", name)
    if not match:
        return ""
    month = int(match.group(1))
    year = 2000 + int(match.group(2))
    if not 1 <= month <= 12:
        return ""
    return f"{year:04d}-{month:02d}"


def future_expiration_from_ticker(ticker: str) -> str:
    match = re.match(r"^[A-Za-z]+([FGHJKMNQUVXZ])(\d)$", ticker)
    if not match:
        return ""
    month = FUTURE_MONTHS.get(match.group(1).upper(), 0)
    year_digit = int(match.group(2))
    current_year = dt.datetime.now(dt.UTC).year
    current_decade = (current_year // 10) * 10
    year = current_decade + year_digit
    if year < current_year - 3:
        year += 10
    return f"{year:04d}-{month:02d}" if month else ""


def compact_asset(asset: dict[str, Any]) -> dict[str, Any] | None:
    asset_type = normalize_text(asset.get("type")).upper()
    if asset_type not in SPOT_TYPES and asset_type != "FUTURES":
        return None
    symbol = normalize_text(asset.get("symbol"))
    ticker = normalize_text(asset.get("ticker"))
    mic = normalize_text(asset.get("mic"))
    if not symbol or not ticker or not mic:
        return None
    name = normalize_text(asset.get("name"))
    is_archived = bool(asset.get("is_archived"))
    market = "futures" if asset_type == "FUTURES" else "spot"
    underlying = ""
    expiration = ""
    if market == "spot":
        underlying = UNDERLYING_OVERRIDES.get(ticker.upper(), ticker.upper())
    else:
        underlying = futures_underlying_from_name(name)
        expiration = normalize_text(asset.get("expiration_date"))[:10]
        if not expiration:
            expiration = future_expiration_from_name(name)
        if not expiration:
            expiration = future_expiration_from_ticker(ticker)
    row = {
        "symbol": symbol,
        "ticker": ticker,
        "name": name,
        "mic": mic,
        "type": asset_type,
        "market": market,
        "underlying": underlying,
        "expiration": expiration,
        "is_archived": is_archived,
    }
    contract_size = asset.get("future_details", {}).get("contract_size") if isinstance(asset.get("future_details"), dict) else None
    if contract_size is not None:
        row["contract_size"] = contract_size
    return row


def sort_key(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        row.get("market") != "spot",
        bool(row.get("is_archived")),
        row.get("underlying") or row.get("ticker"),
        row.get("expiration") or "9999-99",
        row.get("symbol"),
    )


def build_catalog(raw_assets: list[dict[str, Any]], *, complete: bool, next_cursor: str) -> dict[str, Any]:
    rows: list[dict[str, Any]] = []
    seen: set[str] = set()
    for asset in raw_assets:
        row = compact_asset(asset)
        if row is None:
            continue
        symbol = str(row["symbol"])
        if symbol in seen:
            continue
        seen.add(symbol)
        rows.append(row)
    rows.sort(key=sort_key)

    spot_underlyings = {
        str(row.get("underlying"))
        for row in rows
        if row.get("market") == "spot" and row.get("underlying")
    }
    paired_futures = sum(
        1
        for row in rows
        if row.get("market") == "futures" and row.get("underlying") in spot_underlyings
    )
    return {
        "schema": 1,
        "source": "/v1/assets/all",
        "generated_at": dt.datetime.now(dt.UTC).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "complete": complete,
        "next_cursor": "" if complete else next_cursor,
        "raw_asset_count": len(raw_assets),
        "asset_count": len(rows),
        "paired_futures_count": paired_futures,
        "assets": rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", type=Path, default=RECORDER_ENV)
    parser.add_argument("--output", type=Path, default=OUT_PATH)
    parser.add_argument("--max-pages", type=int, default=500)
    parser.add_argument("--retries", type=int, default=3)
    parser.add_argument("--checkpoint", type=Path, default=CHECKPOINT_PATH)
    parser.add_argument("--fresh", action="store_true")
    args = parser.parse_args()

    env = read_env(args.env)
    raw_assets, complete, next_cursor = fetch_assets(env,
                                                     max_pages=args.max_pages,
                                                     retry_count=args.retries,
                                                     checkpoint_path=args.checkpoint,
                                                     fresh=args.fresh)
    catalog = build_catalog(raw_assets, complete=complete, next_cursor=next_cursor)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    tmp = args.output.with_suffix(args.output.suffix + ".tmp")
    tmp.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    tmp.replace(args.output)
    if complete and args.checkpoint.exists():
        args.checkpoint.unlink()
    print(
        f"wrote {args.output} assets={catalog['asset_count']} raw={catalog['raw_asset_count']} "
        f"paired_futures={catalog['paired_futures_count']} complete={catalog['complete']}",
        flush=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
