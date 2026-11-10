#!/usr/bin/env python3
"""Filter a tracerpt XML file-I/O trace to the Stellar Blade process."""

from __future__ import annotations

import argparse
import csv
import ctypes
import datetime as dt
import json
import ntpath
import os
import re
import string
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


PATH_RE = re.compile(
    r'(?i)(?:[a-z]:\\|\\\\\?\\|\\device\\harddiskvolume\d+\\)[^"\x00\r\n<>|]+'
)
HEX_PAYLOAD_RE = re.compile(r"\A\s*(?:[0-9a-fA-F]{2}\s*)+\Z")
MOD_EXTENSIONS = {
    ".pak", ".utoc", ".ucas", ".uasset", ".uexp", ".ubulk", ".uptnl",
    ".dll", ".lua", ".json", ".ini", ".sig",
}


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def child(parent: ET.Element | None, wanted: str) -> ET.Element | None:
    if parent is None:
        return None
    return next((node for node in parent if local_name(node.tag) == wanted), None)


def element_text(node: ET.Element | None) -> str:
    return "" if node is None else "".join(node.itertext()).strip()


def dos_device_map() -> list[tuple[str, str]]:
    if os.name != "nt":
        return []
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    query = kernel32.QueryDosDeviceW
    query.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint]
    query.restype = ctypes.c_uint
    mappings = []
    for letter in string.ascii_uppercase:
        buffer = ctypes.create_unicode_buffer(4096)
        if query(f"{letter}:", buffer, len(buffer)):
            target = buffer.value
            if target.startswith("\\Device\\"):
                mappings.append((target.casefold(), f"{letter}:"))
    return sorted(mappings, key=lambda item: len(item[0]), reverse=True)


def normalize_path(value: str, devices: list[tuple[str, str]]) -> str:
    value = value.strip().strip('"').replace("\x00", "")
    if value.startswith("\\??\\"):
        value = value[4:]
    if value.startswith("\\\\?\\"):
        value = value[4:]
    folded = value.casefold()
    for device, drive in devices:
        if folded.startswith(device + "\\"):
            value = drive + value[len(device):]
            break
    return ntpath.normpath(value)


def extract_paths(text: str, devices: list[tuple[str, str]]) -> list[str]:
    candidates = [text]
    candidates.append(text.replace("\\??\\", "").replace("\\\\?\\", ""))
    found: list[str] = []
    for candidate_text in candidates:
        for match in PATH_RE.finditer(candidate_text):
            path = normalize_path(match.group(0).rstrip(" ,;"), devices)
            ext = ntpath.splitext(path)[1].casefold()
            if ext in MOD_EXTENSIONS or "\\stellarblade\\" in path.casefold():
                if path.casefold() not in {item.casefold() for item in found}:
                    found.append(path)
    return found


def decode_hex_payload(value: str) -> str:
    """Recover UTF-16 strings when tracerpt cannot decode a kernel event schema."""
    value = value.strip()
    if not value or not HEX_PAYLOAD_RE.fullmatch(value):
        return ""
    try:
        payload = bytes.fromhex(value)
    except ValueError:
        return ""
    decoded = []
    for offset in (0, 1):
        usable = payload[offset:len(payload) - ((len(payload) - offset) % 2)]
        if usable:
            decoded.append(usable.decode("utf-16-le", errors="ignore"))
    return "\n".join(decoded)


def canonical(path: str | Path) -> str:
    return ntpath.normcase(ntpath.normpath(str(path)))


def manifest_sources(manifest_path: Path | None, content_root: Path) -> dict[str, str]:
    if manifest_path is None or not manifest_path.is_file():
        return {}
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    result: dict[str, str] = {}
    for entry in manifest.get("files", []):
        if not isinstance(entry, dict):
            continue
        rel_path = entry.get("relPath") or entry.get("relativePath")
        source = str(entry.get("source") or "").strip()
        if not rel_path or not source:
            continue
        target = str(entry.get("target") or "").strip()
        candidates = [content_root / target / rel_path] if target else []
        candidates.append(content_root / rel_path)
        for candidate in candidates:
            result[canonical(candidate)] = source
    return result


def staging_source(path: str, game_root: Path, mods_root: Path | None,
                   cache: dict[str, str]) -> str:
    if mods_root is None or not mods_root.is_dir():
        return ""
    key = canonical(path)
    if key in cache:
        return cache[key]
    game_key = canonical(game_root)
    if not key.startswith(game_key + "\\"):
        cache[key] = ""
        return ""
    relative = ntpath.relpath(key, game_key)
    try:
        mod_dirs = (item for item in mods_root.iterdir() if item.is_dir())
        for mod_dir in mod_dirs:
            staged = mod_dir.joinpath(*relative.split("\\"))
            if staged.is_file():
                cache[key] = mod_dir.name
                return mod_dir.name
    except OSError:
        pass
    cache[key] = ""
    return ""


def parse_pid(value: str) -> int | None:
    try:
        return int(value, 0)
    except (TypeError, ValueError):
        try:
            return int(value, 10)
        except (TypeError, ValueError):
            return None


def parse_xml(xml_path: Path, pid: int, source_map: dict[str, str],
              game_root: Path, mods_root: Path | None) -> list[dict[str, str]]:
    devices = dos_device_map()
    object_paths: dict[str, str] = {}
    stage_cache: dict[str, str] = {}
    rows: list[dict[str, str]] = []

    for _, event in ET.iterparse(xml_path, events=("end",)):
        if local_name(event.tag) != "Event":
            continue
        system = child(event, "System")
        execution = child(system, "Execution")
        event_pid = parse_pid(execution.attrib.get("ProcessID", "")) if execution is not None else None
        if event_pid != pid:
            event.clear()
            continue

        fields: dict[str, list[str]] = {}
        event_data = child(event, "EventData")
        if event_data is not None:
            for node in event_data.iter():
                if local_name(node.tag) == "Data":
                    name = node.attrib.get("Name", "Field")
                    fields.setdefault(name, []).append(element_text(node))
        flat_fields = {name: " | ".join(values) for name, values in fields.items()}

        file_object = ""
        for name, value in flat_fields.items():
            if "fileobject" in name.casefold() or name.casefold() in {"filekey", "filehandle"}:
                file_object = value.strip()
                break
        object_key = f"{pid}:{file_object}" if file_object else ""

        path_candidates: list[str] = []
        for value in flat_fields.values():
            path_candidates.extend(extract_paths(value, devices))
        for node in event.iter():
            if local_name(node.tag) in {"BinaryEventData", "EventPayload"}:
                path_candidates.extend(extract_paths(decode_hex_payload(element_text(node)), devices))
        if not path_candidates:
            path_candidates.extend(extract_paths(element_text(event), devices))
        path_candidates = list(dict.fromkeys(path_candidates))

        if path_candidates and object_key:
            object_paths[object_key] = path_candidates[0]
        if not path_candidates and object_key and object_key in object_paths:
            path_candidates = [object_paths[object_key]]

        if path_candidates:
            system_time = child(system, "TimeCreated")
            execution = child(system, "Execution")
            provider = child(system, "Provider")
            event_id = element_text(child(system, "EventID"))
            task = element_text(child(system, "Task"))
            opcode = element_text(child(system, "Opcode"))
            timestamp = system_time.attrib.get("SystemTime", "") if system_time is not None else ""
            thread_id = execution.attrib.get("ThreadID", "") if execution is not None else ""
            provider_name = provider.attrib.get("Name", "") if provider is not None else ""
            status = next((value for name, value in flat_fields.items()
                           if any(term in name.casefold() for term in ("status", "error", "result"))), "")
            size = next((value for name, value in flat_fields.items()
                         if any(term in name.casefold() for term in ("iosize", "transfersize", "length", "size", "bytes"))), "")
            for path in path_candidates:
                source = source_map.get(canonical(path), "")
                if not source:
                    source = staging_source(path, game_root, mods_root, stage_cache)
                rows.append({
                    "timestamp_utc": timestamp,
                    "pid": str(pid),
                    "tid": thread_id,
                    "provider": provider_name,
                    "event_id": event_id,
                    "task": task,
                    "opcode": opcode,
                    "file_object": file_object,
                    "path": path,
                    "source_mod": source,
                    "size": size,
                    "status": status,
                    "fields_json": json.dumps(flat_fields, ensure_ascii=False),
                })
        event.clear()
    return rows


def is_failure(value: str) -> bool:
    if not value:
        return False
    normalized = value.strip().casefold()
    if any(marker in normalized for marker in (
        "status_success", "status_pending", "status_end_of_file", "success", "s_ok",
    )):
        return False
    numeric = re.match(r"^(?:0x)?([0-9a-f]+)\b", normalized)
    if numeric and int(numeric.group(1), 16) == 0:
        return False
    if normalized.isdecimal() and int(normalized) == 0:
        return False
    return True


def markdown_cell(value: str) -> str:
    return value.replace("|", "\\|").replace("\r", " ").replace("\n", " ")


def write_report(rows: list[dict[str, str]], output_csv: Path, output_md: Path,
                 pid: int, game_root: Path, dropped_events: str) -> None:
    columns = [
        "timestamp_utc", "pid", "tid", "provider", "event_id", "task", "opcode",
        "file_object", "path", "source_mod", "size", "status", "fields_json",
    ]
    with output_csv.open("w", encoding="utf-8-sig", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)

    ordered = sorted(rows, key=lambda row: row["timestamp_utc"])
    source_counts: dict[str, int] = {}
    file_counts: dict[str, int] = {}
    for row in ordered:
        if row["source_mod"]:
            source_counts[row["source_mod"]] = source_counts.get(row["source_mod"], 0) + 1
        file_counts[row["path"]] = file_counts.get(row["path"], 0) + 1
    failures = [row for row in ordered if is_failure(row["status"])]

    lines = [
        "# Stellar Blade file-I/O crash trace",
        "",
        f"- Game process ID: {pid}",
        f"- Game folder: `{game_root}`",
        f"- File-path events captured for the game: {len(ordered)}",
        f"- Distinct paths: {len(file_counts)}",
        f"- Paths mapped to Vortex mods: {sum(bool(row['source_mod']) for row in ordered)} events",
        f"- Events with a non-success status field: {len(failures)}",
        f"- WPR dropped events: {dropped_events}",
        "",
        "## Vortex sources seen in game file activity",
        "",
    ]
    if source_counts:
        lines.extend(["| Mod source | File-I/O events |", "|---|---:|"])
        for source, count in sorted(source_counts.items(), key=lambda item: (-item[1], item[0].casefold())):
            lines.append(f"| {markdown_cell(source)} | {count} |")
    else:
        lines.append("No event path could be matched to a Vortex deployment source.")

    lines.extend(["", "## File operations with a non-success status", ""])
    if failures:
        lines.extend(["| UTC time | Mod source | File | Status | Event |", "|---|---|---|---|---|"])
        for row in failures[-100:]:
            event = f"{row['provider']} / {row['event_id']} / {row['task']} / {row['opcode']}"
            lines.append(
                f"| {row['timestamp_utc']} | {markdown_cell(row['source_mod'] or 'unmapped')} "
                f"| `{markdown_cell(row['path'])}` | {markdown_cell(row['status'])} "
                f"| {markdown_cell(event)} |"
            )
    else:
        lines.append("No non-success status was decoded for file events in this process.")

    lines.extend(["", "## Last 100 file paths accessed by the game", ""])
    if ordered:
        lines.extend(["| UTC time | Mod source | File | Event | Size | Status |", "|---|---|---|---|---:|---|"])
        for row in ordered[-100:]:
            event = f"{row['provider']} / {row['event_id']} / {row['task']} / {row['opcode']}"
            lines.append(
                f"| {row['timestamp_utc']} | {markdown_cell(row['source_mod'] or 'unmapped')} "
                f"| `{markdown_cell(row['path'])}` | {markdown_cell(event)} "
                f"| {markdown_cell(row['size'])} | {markdown_cell(row['status'])} |"
            )
    else:
        lines.append("The ETL conversion contained no path-bearing file events for this PID.")
    lines.extend([
        "",
        "## Reading this report",
        "",
        ("WPR reported dropped events, so missing file activity may not appear here. "
         if dropped_events not in {"0 reported", "not reported"} else "")
        + "A non-success file status can indicate an I/O problem, though the event and status need context. "
        "The last path accessed is only a lead; "
        "successful reads can precede a crash caused by invalid asset data or incompatible code. "
        "Unmapped paths remain in the CSV and can be matched against the Vortex staging folder.",
        "",
    ])
    output_md.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xml", required=True, type=Path, help="XML exported from WPR ETL by tracerpt")
    parser.add_argument("--pid", required=True, type=int, help="SB-Win64-Shipping process ID")
    parser.add_argument("--game-root", required=True, type=Path)
    parser.add_argument("--mods-root", type=Path, help="Vortex staging mods folder")
    parser.add_argument("--manifest", action="append", type=Path, default=[],
                        help="Vortex deployment manifest; may be repeated")
    parser.add_argument("--dropped-events", default="not reported",
                        help="WPR event-loss count, if reported")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    if not args.xml.is_file():
        print(f"Trace XML not found: {args.xml}", file=sys.stderr)
        return 2
    args.output_dir.mkdir(parents=True, exist_ok=True)

    sources: dict[str, str] = {}
    for manifest in args.manifest:
        sources.update(manifest_sources(manifest, manifest.parent))

    try:
        rows = parse_xml(args.xml, args.pid, sources, args.game_root, args.mods_root)
    except (OSError, ET.ParseError, ValueError) as error:
        print(f"Could not parse File-I/O ETL XML: {error}", file=sys.stderr)
        return 5
    csv_path = args.output_dir / "game_fileio.csv"
    md_path = args.output_dir / "fileio_report.md"
    write_report(rows, csv_path, md_path, args.pid, args.game_root, args.dropped_events)
    print(f"File-I/O events for PID {args.pid}: {len(rows)}")
    print(f"Report: {md_path}")
    print(f"CSV: {csv_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
