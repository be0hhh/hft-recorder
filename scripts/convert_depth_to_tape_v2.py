#!/usr/bin/env python3
"""
Convert recorder depth.jsonl sessions to the v2 orderbook tape package.

Input depth rows are the legacy lossless shape:
  [[price, qty, side], [price, qty, side], ..., ts]

Output files:
  depth_tape.jsonl    [tagged_ts, price, qty, price, qty, ...]
  depth_sidecar.jsonl [same_tagged_ts, side, count, side, count, ...]
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path
from typing import Iterable


TS_TAG = 1 << 63
TS_MASK = TS_TAG - 1
DEPTH_TAPE_NAME = "depth_tape.jsonl"
DEPTH_SIDECAR_NAME = "depth_sidecar.jsonl"
LEGACY_DEPTH_NAME = "depth.jsonl"


class ConvertError(ValueError):
    pass


def tagged_ts(ts: int) -> int:
    if ts < 0 or ts > TS_MASK:
        raise ConvertError(f"timestamp out of 63-bit range: {ts}")
    return TS_TAG | ts


def parse_legacy_depth_row(row: object, line_no: int) -> tuple[int, list[list[int]]]:
    if not isinstance(row, list) or not row:
        raise ConvertError(f"line {line_no}: expected non-empty JSON array")
    ts = row[-1]
    if not isinstance(ts, int):
        raise ConvertError(f"line {line_no}: last item must be integer timestamp")
    levels: list[list[int]] = []
    for index, item in enumerate(row[:-1]):
        if not isinstance(item, list) or len(item) != 3:
            raise ConvertError(f"line {line_no}: level {index} must be [price, qty, side]")
        price, qty, side = item
        if not isinstance(price, int) or not isinstance(qty, int) or not isinstance(side, int):
            raise ConvertError(f"line {line_no}: level {index} fields must be integers")
        if price < 0 or qty < 0:
            raise ConvertError(f"line {line_no}: level {index} price/qty must be non-negative")
        if side not in (0, 1):
            raise ConvertError(f"line {line_no}: level {index} side must be 0 or 1")
        levels.append([price, qty, side])
    return ts, levels


def make_tape_row(ts: int, levels: Iterable[list[int]]) -> list[int]:
    out = [tagged_ts(ts)]
    for price, qty, _side in levels:
        out.append(price)
        out.append(qty)
    return out


def make_rle_sidecar_row(ts: int, levels: Iterable[list[int]]) -> list[int]:
    out = [tagged_ts(ts)]
    run_side: int | None = None
    run_count = 0
    for _price, _qty, side in levels:
        if run_side is None:
            run_side = side
            run_count = 1
            continue
        if side == run_side:
            run_count += 1
            continue
        out.append(run_side)
        out.append(run_count)
        run_side = side
        run_count = 1
    if run_side is not None:
        out.append(run_side)
        out.append(run_count)
    return out


def convert_depth_file(
    src: Path,
    tape_dst: Path,
    sidecar_dst: Path,
    skip_corrupt_lines: bool,
) -> tuple[int, int, list[dict[str, object]]]:
    rows = 0
    levels_total = 0
    skipped_lines: list[dict[str, object]] = []
    tape_dst.parent.mkdir(parents=True, exist_ok=True)
    sidecar_dst.parent.mkdir(parents=True, exist_ok=True)
    tape_tmp = tape_dst.with_name(tape_dst.name + ".tmp")
    sidecar_tmp = sidecar_dst.with_name(sidecar_dst.name + ".tmp")
    def skip_line(line_no: int, byte_offset: int, reason: str) -> None:
        skipped_lines.append({"line": line_no, "byte_offset": byte_offset, "reason": reason})
        print(f"skipping corrupt depth line {line_no}: {reason}", file=sys.stderr)

    try:
        with src.open("rb") as inp, \
             tape_tmp.open("w", encoding="utf-8", newline="\n") as tape_out, \
             sidecar_tmp.open("w", encoding="utf-8", newline="\n") as side_out:
            offset = 0
            for line_no, raw_line in enumerate(inp, 1):
                line_offset = offset
                offset += len(raw_line)
                stripped_bytes = raw_line.strip()
                if not stripped_bytes:
                    continue
                try:
                    stripped = stripped_bytes.decode("utf-8")
                    parsed = json.loads(stripped)
                except UnicodeDecodeError as exc:
                    if not skip_corrupt_lines:
                        raise ConvertError(
                            f"line {line_no}: invalid utf-8 at byte offset {line_offset + exc.start}"
                        ) from exc
                    skip_line(line_no, line_offset + exc.start, "invalid utf-8")
                    continue
                except json.JSONDecodeError as exc:
                    if not skip_corrupt_lines:
                        raise ConvertError(
                            f"line {line_no}: invalid JSON at column {exc.colno}"
                        ) from exc
                    skip_line(line_no, line_offset, f"invalid JSON at column {exc.colno}")
                    continue

                try:
                    ts, levels = parse_legacy_depth_row(parsed, line_no)
                except ConvertError as exc:
                    if not skip_corrupt_lines:
                        raise
                    skip_line(line_no, line_offset, str(exc))
                    continue
                tape_out.write(json.dumps(make_tape_row(ts, levels), separators=(",", ":")))
                tape_out.write("\n")
                side_out.write(json.dumps(make_rle_sidecar_row(ts, levels), separators=(",", ":")))
                side_out.write("\n")
                rows += 1
                levels_total += len(levels)
    except Exception:
        tape_tmp.unlink(missing_ok=True)
        sidecar_tmp.unlink(missing_ok=True)
        raise
    tape_tmp.replace(tape_dst)
    sidecar_tmp.replace(sidecar_dst)
    return rows, levels_total, skipped_lines


def copy_session_tree(src_dir: Path, dst_dir: Path, keep_legacy_depth: bool) -> None:
    def ignore(_dir: str, names: list[str]) -> set[str]:
        ignored = {DEPTH_TAPE_NAME, DEPTH_SIDECAR_NAME, "seek_index.json"}
        if not keep_legacy_depth:
            ignored.add(LEGACY_DEPTH_NAME)
        return {name for name in names if name in ignored}

    shutil.copytree(src_dir, dst_dir, dirs_exist_ok=True, ignore=ignore)


def find_depth_file(session_dir: Path) -> Path:
    jsonl_depth = session_dir / "jsonl" / "depth.jsonl"
    if jsonl_depth.is_file():
        return jsonl_depth
    root_depth = session_dir / "depth.jsonl"
    if root_depth.is_file():
        return root_depth
    raise ConvertError(f"depth.jsonl not found under {session_dir}")


def output_paths(input_depth: Path, output_dir: Path) -> tuple[Path, Path]:
    if input_depth.parent.name == "jsonl":
        base = output_dir / "jsonl"
    else:
        base = output_dir
    return base / DEPTH_TAPE_NAME, base / DEPTH_SIDECAR_NAME


def cleanup_output_package(output_dir: Path, input_depth: Path, keep_legacy_depth: bool) -> None:
    if not keep_legacy_depth:
        legacy_parent = output_dir / "jsonl" if input_depth.parent.name == "jsonl" else output_dir
        legacy_path = legacy_parent / LEGACY_DEPTH_NAME
        if legacy_path.exists():
            legacy_path.unlink()
    seek_index = output_dir / "seek_index.json"
    if seek_index.exists():
        seek_index.unlink()


def update_manifest(output_dir: Path, tape_dst: Path, sidecar_dst: Path, rows: int) -> bool:
    manifest_path = output_dir / "manifest.json"
    if not manifest_path.is_file():
        return False

    with manifest_path.open("r", encoding="utf-8") as inp:
        manifest = json.load(inp)

    tape_rel = tape_dst.relative_to(output_dir).as_posix()
    sidecar_rel = sidecar_dst.relative_to(output_dir).as_posix()

    manifest["capture_contract_version"] = "hftrec.strict_canonical_rows_json.v2"
    channels = manifest.setdefault("channels", {})
    depth = channels.setdefault("depth", {})
    depth["path"] = tape_rel
    depth["sidecar_path"] = sidecar_rel
    depth["row_schema"] = "cxet_orderbook_tape_rle_sidecar_v1"
    depth["declared_event_count"] = rows

    artifacts = manifest.setdefault("artifacts", {})
    canonical = artifacts.setdefault("canonical", [])
    if not isinstance(canonical, list):
        canonical = []

    legacy_paths = {
        LEGACY_DEPTH_NAME,
        f"jsonl/{LEGACY_DEPTH_NAME}",
        DEPTH_TAPE_NAME,
        f"jsonl/{DEPTH_TAPE_NAME}",
        DEPTH_SIDECAR_NAME,
        f"jsonl/{DEPTH_SIDECAR_NAME}",
    }
    next_canonical = [item for item in canonical if isinstance(item, str) and item not in legacy_paths]
    for item in (tape_rel, sidecar_rel):
        if item not in next_canonical:
            next_canonical.append(item)
    artifacts["canonical"] = next_canonical

    with manifest_path.open("w", encoding="utf-8", newline="\n") as out:
        json.dump(manifest, out, ensure_ascii=False, indent=2)
        out.write("\n")
    return True


def write_migration_report(
    output_dir: Path,
    input_dir: Path,
    input_depth: Path,
    tape_dst: Path,
    sidecar_dst: Path,
    rows: int,
    levels: int,
    skipped_lines: list[dict[str, object]],
    manifest_updated: bool,
) -> Path:
    reports_dir = output_dir / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)
    report_path = reports_dir / "depth_migration_report.json"
    report = {
        "schema": "hftrec.depth_migration_report.v1",
        "input_session": str(input_dir),
        "input_depth": str(input_depth),
        "output_session": str(output_dir),
        "output_tape": tape_dst.relative_to(output_dir).as_posix(),
        "output_sidecar": sidecar_dst.relative_to(output_dir).as_posix(),
        "depth_row_schema": "cxet_orderbook_tape_rle_sidecar_v1",
        "rows_written": rows,
        "levels_written": levels,
        "skipped_count": len(skipped_lines),
        "skipped_lines": skipped_lines,
        "manifest_updated": manifest_updated,
        "legacy_depth_removed": not (output_dir / input_depth.relative_to(input_dir)).exists(),
    }
    with report_path.open("w", encoding="utf-8", newline="\n") as out:
        json.dump(report, out, ensure_ascii=False, indent=2)
        out.write("\n")
    return report_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Convert legacy recorder depth.jsonl to orderbook tape v2 package.")
    parser.add_argument("input", type=Path, help="Input session directory containing depth.jsonl or jsonl/depth.jsonl")
    parser.add_argument("output", type=Path, help="Output session directory")
    parser.add_argument("--no-copy", action="store_true", help="Do not copy the rest of the session tree before writing v2 files")
    parser.add_argument("--keep-legacy-depth", action="store_true", help="Copy legacy depth.jsonl into the output package too")
    parser.add_argument("--skip-corrupt-lines", action="store_true", help="Skip legacy depth lines that are not valid UTF-8 or JSON")
    args = parser.parse_args()

    input_dir = args.input.resolve()
    output_dir = args.output.resolve()
    if not input_dir.exists() or not input_dir.is_dir():
        raise ConvertError(f"input must be an existing directory: {input_dir}")
    if input_dir == output_dir:
        raise ConvertError("input and output must be different directories")

    input_depth = find_depth_file(input_dir)
    if not args.no_copy:
        copy_session_tree(input_dir, output_dir, args.keep_legacy_depth)
    cleanup_output_package(output_dir, input_depth, args.keep_legacy_depth)
    tape_dst, sidecar_dst = output_paths(input_depth, output_dir)
    rows, levels, skipped_lines = convert_depth_file(input_depth, tape_dst, sidecar_dst, args.skip_corrupt_lines)
    manifest_updated = update_manifest(output_dir, tape_dst, sidecar_dst, rows)
    report_path = write_migration_report(
        output_dir,
        input_dir,
        input_depth,
        tape_dst,
        sidecar_dst,
        rows,
        levels,
        skipped_lines,
        manifest_updated,
    )
    print(
        f"converted rows={rows} levels={levels} tape={tape_dst} "
        f"sidecar={sidecar_dst} skipped={len(skipped_lines)} manifest_updated={int(manifest_updated)} "
        f"report={report_path}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
