#!/usr/bin/env python3
"""Convert legacy backtest orders.jsonl streams to compact order_lifetimes.jsonl."""

from __future__ import annotations

import argparse
import json
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ACTION_ORDER = 1
ACTION_AMEND = 2
ACTION_CANCEL = 3
TYPE_LIMIT = 1
STATUS_ACKED = 2
STATUS_FILLED = 3
STATUS_CANCELLED = 4


@dataclass(frozen=True)
class OrderRow:
    order_id: int
    target_id: int
    active_ts_ns: int
    action: int
    side: int
    order_type: int
    status: int
    price_e8: int
    qty_e8: int
    leg_index: int


@dataclass(frozen=True)
class FillRow:
    order_id: int
    exit_ts_ns: int


@dataclass(frozen=True)
class LifetimeRow:
    ts_start_ns: int
    ts_end_ns: int
    price_e8: int
    qty_e8: int
    side: int
    open_ended: int
    leg_index: int


def parse_int(value: Any) -> int:
    if isinstance(value, bool):
        raise ValueError("bool is not an int field")
    if not isinstance(value, int):
        raise ValueError(f"expected int field, got {type(value).__name__}")
    return value


def read_jsonl_arrays(path: Path) -> list[list[Any]]:
    rows: list[list[Any]] = []
    if not path.exists():
        return rows
    with path.open("r", encoding="utf-8") as handle:
        for line_no, line in enumerate(handle, 1):
            text = line.strip()
            if not text:
                continue
            row = json.loads(text)
            if not isinstance(row, list):
                raise ValueError(f"{path}:{line_no}: expected array row")
            rows.append(row)
    return rows


def parse_order(row: list[Any]) -> OrderRow:
    if len(row) < 12:
        raise ValueError("legacy order row has fewer than 12 fields")
    return OrderRow(
        order_id=parse_int(row[0]),
        target_id=parse_int(row[1]),
        active_ts_ns=parse_int(row[3]),
        action=parse_int(row[5]),
        side=parse_int(row[6]),
        order_type=parse_int(row[7]),
        status=parse_int(row[8]),
        price_e8=parse_int(row[9]),
        qty_e8=parse_int(row[10]),
        leg_index=parse_int(row[12]) if len(row) > 12 else 0,
    )


def parse_fill(row: list[Any]) -> FillRow:
    if len(row) < 3:
        raise ValueError("fill row has fewer than 3 fields")
    return FillRow(order_id=parse_int(row[0]), exit_ts_ns=parse_int(row[2]))


def is_effective_status(status: int) -> bool:
    return status in (STATUS_ACKED, STATUS_FILLED, STATUS_CANCELLED)


def build_lifetimes(orders: list[OrderRow], fills: list[FillRow], fallback_end_ts_ns: int) -> list[LifetimeRow]:
    fill_by_order_id: dict[int, FillRow] = {}
    for fill in fills:
        if fill.exit_ts_ns <= 0:
            continue
        previous = fill_by_order_id.get(fill.order_id)
        if previous is None or fill.exit_ts_ns < previous.exit_ts_ns:
            fill_by_order_id[fill.order_id] = fill

    commands_by_target_id: dict[int, list[OrderRow]] = {}
    for order in orders:
        if order.target_id == 0 or order.active_ts_ns <= 0:
            continue
        if order.action not in (ACTION_AMEND, ACTION_CANCEL):
            continue
        if not is_effective_status(order.status):
            continue
        commands_by_target_id.setdefault(order.target_id, []).append(order)

    for commands in commands_by_target_id.values():
        commands.sort(key=lambda row: (row.active_ts_ns, row.order_id))

    lifetimes: list[LifetimeRow] = []
    for order in orders:
        if order.action != ACTION_ORDER or order.order_type != TYPE_LIMIT:
            continue
        if not is_effective_status(order.status):
            continue
        if order.active_ts_ns <= 0 or order.price_e8 <= 0 or order.qty_e8 <= 0:
            continue

        commands = commands_by_target_id.get(order.order_id, [])
        if any(command.active_ts_ns == order.active_ts_ns for command in commands):
            continue

        end_ts = 0
        for command in commands:
            if command.active_ts_ns > order.active_ts_ns:
                end_ts = command.active_ts_ns
                break

        fill = fill_by_order_id.get(order.order_id)
        if fill is not None and (end_ts == 0 or fill.exit_ts_ns < end_ts):
            end_ts = fill.exit_ts_ns

        open_ended = 0
        if end_ts <= order.active_ts_ns:
            if fill is not None:
                end_ts = order.active_ts_ns
            elif order.status == STATUS_ACKED and fallback_end_ts_ns > order.active_ts_ns:
                end_ts = fallback_end_ts_ns
                open_ended = 1

        if end_ts <= order.active_ts_ns:
            continue

        lifetimes.append(
            LifetimeRow(
                ts_start_ns=order.active_ts_ns,
                ts_end_ns=end_ts,
                price_e8=order.price_e8,
                qty_e8=order.qty_e8,
                side=order.side,
                open_ended=open_ended,
                leg_index=order.leg_index,
            )
        )

    lifetimes.sort(key=lambda row: (row.ts_start_ns, row.price_e8))
    return lifetimes


def final_risk_ts(path: Path) -> int:
    rows = read_jsonl_arrays(path)
    last_ts = 0
    for row in rows:
        if row:
            last_ts = max(last_ts, parse_int(row[0]))
    return last_ts


def write_lifetimes(path: Path, rows: list[LifetimeRow]) -> None:
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    with tmp_path.open("w", encoding="utf-8", newline="\n") as handle:
        for row in rows:
            handle.write(
                json.dumps(
                    [
                        row.ts_start_ns,
                        row.ts_end_ns,
                        row.price_e8,
                        row.qty_e8,
                        row.side,
                        row.open_ended,
                        row.leg_index,
                    ],
                    separators=(",", ":"),
                )
            )
            handle.write("\n")
    os.replace(tmp_path, path)


def update_manifest(path: Path, rows: int, keep_orders: bool) -> None:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    streams = manifest.setdefault("streams", {})
    if isinstance(streams, dict):
        if not keep_orders:
            streams.pop("orders", None)
        streams["order_lifetimes"] = {
            "path": "order_lifetimes.jsonl",
            "row_schema": "[ts_start_ns,ts_end_ns,price_e8,qty_e8,side,open_ended,leg_index]",
            "rows": rows,
        }
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    tmp_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(tmp_path, path)


def find_backtest_dirs(root: Path) -> list[Path]:
    if (root / "orders.jsonl").is_file() and (root / "manifest.json").is_file():
        return [root]
    return sorted(
        path.parent
        for path in root.rglob("orders.jsonl")
        if "sweeps" not in path.parts and path.parent.joinpath("manifest.json").is_file()
    )


def migrate_one(path: Path, apply: bool, keep_orders: bool) -> tuple[bool, str]:
    orders_path = path / "orders.jsonl"
    lifetimes_path = path / "order_lifetimes.jsonl"
    if not orders_path.exists():
        return False, "skip: no orders.jsonl"
    if lifetimes_path.exists():
        return False, "skip: order_lifetimes.jsonl already exists"

    orders = [parse_order(row) for row in read_jsonl_arrays(orders_path)]
    fills = [parse_fill(row) for row in read_jsonl_arrays(path / "fills.jsonl")]
    fallback_end = final_risk_ts(path / "risk_snapshots.jsonl")
    lifetimes = build_lifetimes(orders, fills, fallback_end)

    if not apply:
        return True, f"dry-run: {len(orders)} raw rows -> {len(lifetimes)} lifetimes"

    write_lifetimes(lifetimes_path, lifetimes)
    update_manifest(path / "manifest.json", len(lifetimes), keep_orders)
    if not keep_orders:
        orders_path.unlink()
    suffix = " kept legacy orders" if keep_orders else " deleted legacy orders"
    return True, f"migrated: {len(orders)} raw rows -> {len(lifetimes)} lifetimes;{suffix}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    default_root = Path(__file__).resolve().parents[1] / "recordings"
    parser.add_argument("root", nargs="?", type=Path, default=default_root)
    parser.add_argument("--apply", action="store_true", help="write lifetimes, update manifests, delete legacy orders.jsonl")
    parser.add_argument("--keep-orders", action="store_true", help="with --apply, keep legacy orders.jsonl and its manifest stream")
    args = parser.parse_args()

    root = args.root.resolve()
    if not root.exists():
        print(f"missing root: {root}")
        return 2

    changed = 0
    failed = 0
    for path in find_backtest_dirs(root):
        try:
            did_change, message = migrate_one(path, args.apply, args.keep_orders)
        except Exception as exc:  # noqa: BLE001 - migration must continue across result dirs.
            failed += 1
            print(f"{path}: error: {exc}")
            continue
        if did_change:
            changed += 1
        print(f"{path}: {message}")

    mode = "apply" if args.apply else "dry-run"
    print(f"{mode}: changed={changed} failed={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
