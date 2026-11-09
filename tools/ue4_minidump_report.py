#!/usr/bin/env python3
"""Read-only analyzer for Unreal Engine Windows crash reports and minidumps."""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path


MINIDUMP_SIGNATURE = 0x504D444D
STREAM_NAMES = {
    3: "ThreadListStream",
    4: "ModuleListStream",
    5: "MemoryListStream",
    6: "ExceptionStream",
    7: "SystemInfoStream",
    9: "Memory64ListStream",
    15: "MiscInfoStream",
    24: "ThreadNamesStream",
}
REGISTERS_X64 = (
    "Rax", "Rcx", "Rdx", "Rbx", "Rsp", "Rbp", "Rsi", "Rdi",
    "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "Rip",
)
VORTEX_TIMESTAMP = re.compile(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d+)?Z)")
STACK_FRAME = re.compile(r"^\s*(.*?)\s+0x([0-9a-fA-F]+)\s+\+\s+([0-9a-fA-F]+)\s*$")


def checked_slice(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"Invalid {label} range: offset={offset}, size={size}")
    return data[offset:offset + size]


def parse_minidump(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 32:
        raise ValueError("File is too small to be a Windows minidump")

    signature, version, stream_count, directory_rva, checksum, timestamp, flags = (
        struct.unpack_from("<IIIIIIQ", data, 0)
    )
    if signature != MINIDUMP_SIGNATURE:
        raise ValueError(f"Invalid minidump signature: 0x{signature:08x}")
    if stream_count > 4096:
        raise ValueError(f"Unreasonable minidump stream count: {stream_count}")
    checked_slice(data, directory_rva, stream_count * 12, "stream directory")

    streams = {}
    stream_list = []
    for index in range(stream_count):
        stream_type, size, rva = struct.unpack_from("<III", data, directory_rva + index * 12)
        checked_slice(data, rva, size, f"stream {stream_type}")
        streams.setdefault(stream_type, (size, rva))
        stream_list.append((stream_type, size, rva))

    result = {
        "path": path,
        "size": len(data),
        "version": version,
        "timestamp": timestamp,
        "flags": flags,
        "streams": stream_list,
        "exception": None,
        "registers": {},
        "modules": [],
        "memory_ranges": [],
        "system_arch": None,
    }

    exception_stream = streams.get(6)
    if exception_stream:
        size, rva = exception_stream
        if size < 168:
            raise ValueError("Exception stream is truncated")
        thread_id = struct.unpack_from("<I", data, rva)[0]
        exception_code, exception_flags, chained_record, address, parameter_count = (
            struct.unpack_from("<IIQQI", data, rva + 8)
        )
        parameter_count = min(parameter_count, 15)
        information = struct.unpack_from("<15Q", data, rva + 40)
        context_size, context_rva = struct.unpack_from("<II", data, rva + 160)
        checked_slice(data, context_rva, context_size, "exception context")
        result["exception"] = {
            "thread_id": thread_id,
            "code": exception_code,
            "flags": exception_flags,
            "record": chained_record,
            "address": address,
            "parameter_count": parameter_count,
            "information": information[:parameter_count],
            "context_size": context_size,
        }

        if context_size >= 256:
            context = data[context_rva:context_rva + context_size]
            values = struct.unpack_from("<17Q", context, 120)
            result["registers"] = dict(zip(REGISTERS_X64, values))

    module_stream = streams.get(4)
    if module_stream:
        size, rva = module_stream
        if size >= 4:
            count = struct.unpack_from("<I", data, rva)[0]
            if count > (size - 4) // 108:
                raise ValueError("Module list is truncated")
            for index in range(count):
                offset = rva + 4 + index * 108
                base, image_size = struct.unpack_from("<QI", data, offset)
                name_rva = struct.unpack_from("<I", data, offset + 20)[0]
                cv_size, cv_rva = struct.unpack_from("<II", data, offset + 76)
                name = read_minidump_string(data, name_rva)
                module = {"base": base, "size": image_size, "name": name, "pdb": None}
                if cv_size:
                    cv_data = checked_slice(data, cv_rva, cv_size, "CodeView record")
                    module["pdb"] = parse_codeview(cv_data)
                result["modules"].append(module)

    memory_stream = streams.get(5)
    if memory_stream:
        size, rva = memory_stream
        if size >= 4:
            count = struct.unpack_from("<I", data, rva)[0]
            if count > (size - 4) // 16:
                raise ValueError("Memory list is truncated")
            for index in range(count):
                offset = rva + 4 + index * 16
                start = struct.unpack_from("<Q", data, offset)[0]
                data_size, data_rva = struct.unpack_from("<II", data, offset + 8)
                checked_slice(data, data_rva, data_size, "memory range")
                result["memory_ranges"].append((start, data_size, data_rva))

    memory64_stream = streams.get(9)
    if memory64_stream:
        size, rva = memory64_stream
        if size >= 16:
            count, data_rva = struct.unpack_from("<QQ", data, rva)
            if count > (size - 16) // 16:
                raise ValueError("Memory64 list is truncated")
            for index in range(count):
                start, data_size = struct.unpack_from("<QQ", data, rva + 16 + index * 16)
                checked_slice(data, data_rva, data_size, "Memory64 range")
                result["memory_ranges"].append((start, data_size, data_rva))
                data_rva += data_size

    system_stream = streams.get(7)
    if system_stream and system_stream[0] >= 2:
        result["system_arch"] = struct.unpack_from("<H", data, system_stream[1])[0]

    result["memory_reader"] = lambda address, size: read_dump_memory(
        data, result["memory_ranges"], address, size
    )
    return result


def read_minidump_string(data: bytes, rva: int) -> str:
    if rva == 0:
        return "<unknown>"
    length = struct.unpack("<I", checked_slice(data, rva, 4, "string length"))[0]
    if length > 1024 * 1024:
        raise ValueError("Unreasonable minidump string length")
    return checked_slice(data, rva + 4, length, "minidump string").decode("utf-16le", "replace")


def parse_codeview(data: bytes) -> dict:
    if data.startswith(b"RSDS") and len(data) >= 24:
        guid = uuid.UUID(bytes_le=data[4:20])
        age = struct.unpack_from("<I", data, 20)[0]
        pdb_path = data[24:].split(b"\0", 1)[0].decode("utf-8", "replace")
        return {"kind": "RSDS", "guid": str(guid), "age": age, "path": pdb_path}
    if data.startswith(b"NB10") and len(data) >= 16:
        signature, timestamp, age = struct.unpack_from("<III", data, 4)
        pdb_path = data[16:].split(b"\0", 1)[0].decode("utf-8", "replace")
        return {
            "kind": "NB10", "signature": signature, "timestamp": timestamp,
            "age": age, "path": pdb_path,
        }
    return {"kind": data[:4].decode("ascii", "replace"), "path": ""}


def read_dump_memory(data: bytes, ranges: list, address: int, size: int):
    for start, length, rva in ranges:
        if start <= address and address + size <= start + length:
            offset = rva + address - start
            return data[offset:offset + size]
    return None


def read_xml_context(crash_dir: Path) -> dict:
    path = crash_dir / "CrashContext.runtime-xml"
    if not path.exists():
        return {"path": path, "values": {}, "stack": []}
    root = ET.parse(path).getroot()
    wanted = (
        "ErrorMessage", "SecondsSinceStart", "PCallStackHash", "GameName",
        "ExecutableName", "CommandLine", "GameStateName", "ProcessId",
        "ThreadID", "ThreadName", "CrashType",
    )
    values = {}
    for tag in wanted:
        node = root.find(f".//{tag}")
        if node is not None and node.text:
            values[tag] = node.text.strip()
    stack_node = root.find(".//PCallStack")
    stack = []
    if stack_node is not None and stack_node.text:
        for line in stack_node.text.splitlines():
            match = STACK_FRAME.match(line)
            if match:
                stack.append({
                    "reported_module": match.group(1).strip(),
                    "base": int(match.group(2), 16),
                    "offset": int(match.group(3), 16),
                })
    return {"path": path, "values": values, "stack": stack}


def parse_exception_code(code: int, information: list) -> str:
    if code == 0xC0000005:
        access = information[0] if information else None
        address = information[1] if len(information) > 1 else None
        access_name = {0: "read", 1: "write", 8: "execute"}.get(access, "unknown access")
        target = f" at 0x{address:016x}" if address is not None else ""
        return f"EXCEPTION_ACCESS_VIOLATION ({access_name}{target})"
    if code == 0xC0000409:
        return "STATUS_STACK_BUFFER_OVERRUN / fast-fail"
    if code == 0x80000003:
        return "EXCEPTION_BREAKPOINT"
    if code == 0xC00000FD:
        return "STATUS_STACK_OVERFLOW"
    return f"Windows exception 0x{code:08x}"


def find_module(modules: list, address: int):
    for module in modules:
        if module["base"] <= address < module["base"] + module["size"]:
            return module
    return None


def format_time(value: dt.datetime | None) -> str:
    if value is None:
        return "unknown"
    return value.astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")


def parse_vortex_activity(crash_time: dt.datetime):
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return {}
    vortex_dir = Path(appdata) / "Vortex"
    events = []
    if not vortex_dir.exists():
        return {}

    for log_path in vortex_dir.glob("vortex*.log"):
        try:
            lines = log_path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        for line in lines:
            stamp_match = VORTEX_TIMESTAMP.match(line)
            if not stamp_match:
                continue
            try:
                stamp = dt.datetime.fromisoformat(stamp_match.group(1)[:-1] + "+00:00")
            except ValueError:
                continue
            if stamp.astimezone() > crash_time.astimezone():
                continue

            if "Enabled mods at game launch:" in line:
                match = re.search(r"Enabled mods at game launch:\s*(\d+)", line)
                if match:
                    events.append({
                        "kind": "launch",
                        "time": stamp,
                        "enabled_mod_count": int(match.group(1)),
                    })
            marker = "Event tracked "
            if marker in line:
                payload = line.split(marker, 1)[1]
                try:
                    event = json.loads(payload)
                except json.JSONDecodeError:
                    continue
                if event.get("eventName") == "mods_deployed":
                    properties = event.get("properties") or {}
                    events.append({
                        "kind": "deploy",
                        "time": stamp,
                        "file_count": properties.get("file_count"),
                        "enabled_mod_count": properties.get("enabled_mod_count"),
                        "deployment_method": properties.get("deployment_method"),
                    })

    events.sort(key=lambda item: item["time"])
    last_deploy = next((item for item in reversed(events) if item["kind"] == "deploy"), None)
    last_launch = next((item for item in reversed(events) if item["kind"] == "launch"), None)
    return {"deploy": last_deploy, "launch": last_launch}


def read_vortex_manifest(game_dir: Path) -> dict:
    path = game_dir / "SB" / "Content" / "Paks" / "~mods" / "vortex.deployment.stellarblade-uesortablepak.json"
    if not path.exists():
        return {}
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {"path": path, "error": "Could not read deployment manifest"}
    files = manifest.get("files") or []
    sources = sorted({
        str(item.get("source", "")).strip()
        for item in files
        if isinstance(item, dict) and item.get("source")
    })
    deployment_time = manifest.get("deploymentTime")
    when = None
    if isinstance(deployment_time, (int, float)):
        when = dt.datetime.fromtimestamp(deployment_time / 1000, tz=dt.timezone.utc).astimezone()
    return {
        "path": path,
        "time": when,
        "file_count": len(files),
        "sources": sources,
    }


def discover_crash_path(argument: str | None) -> Path:
    if argument:
        path = Path(argument).expanduser()
        if not path.exists():
            raise FileNotFoundError(f"Crash path does not exist: {path}")
        if path.is_dir():
            return path
        return path

    local_app_data = os.environ.get("LOCALAPPDATA")
    if not local_app_data:
        raise FileNotFoundError("LOCALAPPDATA is not set; pass a crash folder or dump path")
    root = Path(local_app_data) / "SB" / "Saved" / "Crashes"
    candidates = [
        item for item in root.iterdir()
        if item.is_dir() and (item / "UE4Minidump.dmp").exists()
    ] if root.exists() else []
    if not candidates:
        raise FileNotFoundError(f"No UE4 minidumps found under {root}")
    return max(
        candidates,
        key=lambda item: (item / "CrashContext.runtime-xml").stat().st_mtime
        if (item / "CrashContext.runtime-xml").exists() else item.stat().st_mtime,
    )


def resolve_executable(explicit: Path | None, modules: list, game_dir: Path) -> Path | None:
    if explicit:
        return explicit if explicit.exists() else None
    for module in modules:
        if Path(module["name"]).name.lower() == "sb-win64-shipping.exe":
            path = Path(module["name"])
            if path.exists():
                return path
    candidate = game_dir / "SB" / "Binaries" / "Win64" / "SB-Win64-Shipping.exe"
    return candidate if candidate.exists() else None


def disassemble(objdump: str | None, executable: Path | None, address: int) -> list[str]:
    if not objdump or not executable:
        return []
    start = max(0, address - 0x40)
    stop = address + 0x60
    command = [
        objdump, "-d", "-M", "intel",
        f"--start-address=0x{start:x}", f"--stop-address=0x{stop:x}",
        str(executable),
    ]
    try:
        completed = subprocess.run(
            command, capture_output=True, text=True, errors="replace", timeout=30, check=False
        )
    except (OSError, subprocess.TimeoutExpired):
        return []
    if completed.returncode != 0:
        return []
    lines = completed.stdout.splitlines()
    instruction_line = None
    for index, line in enumerate(lines):
        match = re.match(r"\s*([0-9a-fA-F]+):", line)
        if match and int(match.group(1), 16) == address:
            instruction_line = index
            break
    if instruction_line is None:
        return []
    return lines[max(0, instruction_line - 5):instruction_line + 5]


def build_report(crash_dir: Path, game_dir: Path, explicit_exe: Path | None,
                 objdump: str | None, mod_matches: list[str]) -> str:
    dump_path = crash_dir if crash_dir.is_file() else crash_dir / "UE4Minidump.dmp"
    if dump_path.is_file():
        crash_dir = dump_path.parent
    if not dump_path.exists():
        raise FileNotFoundError(f"UE4Minidump.dmp not found at {dump_path}")

    crash_time = dt.datetime.fromtimestamp(dump_path.stat().st_mtime).astimezone()
    xml = read_xml_context(crash_dir)
    dump = parse_minidump(dump_path)
    exception = dump["exception"]
    if exception is None:
        raise ValueError("The dump has no ExceptionStream")

    registers = dump["registers"]
    rip = registers.get("Rip", exception["address"])
    fault_module = find_module(dump["modules"], rip)
    executable = resolve_executable(explicit_exe, dump["modules"], game_dir)
    access_description = parse_exception_code(exception["code"], exception["information"])
    vortex_activity = parse_vortex_activity(crash_time)
    vortex_manifest = read_vortex_manifest(game_dir)
    system_arch = {0: "x86", 9: "AMD64/x64", 5: "ARM32", 12: "ARM64"}.get(
        dump["system_arch"], str(dump["system_arch"])
    )

    lines = [
        "# UE4 crash analysis",
        "",
        f"- Crash folder: {crash_dir}",
        f"- Minidump: {dump_path.name} ({dump['size']:,} bytes)",
        f"- Crash time: {format_time(crash_time)}",
        f"- Time since process start: {xml['values'].get('SecondsSinceStart', 'unknown')} s",
        f"- Game state: {xml['values'].get('GameStateName', 'unknown')}",
        f"- Exception thread: {xml['values'].get('ThreadName', 'unknown')} "
        f"(ID {exception['thread_id']})",
        f"- Exception: {access_description} (0x{exception['code']:08x})",
        f"- Faulting instruction: 0x{rip:016x}",
        f"- PCallStack hash: {xml['values'].get('PCallStackHash', 'unavailable')}",
        f"- Minidump architecture: {system_arch}",
        "",
        "## Exception details",
        "",
    ]

    if (
        exception["code"] == 0xC0000005
        and len(exception["information"]) > 1
        and exception["information"][0] == 0
        and exception["information"][1] == 0
    ):
        lines.extend([
            "## Initial interpretation",
            "",
            "- Windows recorded a read through address 0x0 (null-pointer dereference). "
            "This identifies the invalid access, but not what made the pointer null.",
            "",
        ])

    if fault_module:
        lines.append(
            f"- Faulting module: {fault_module['name']} "
            f"+ 0x{rip - fault_module['base']:x} "
            f"(image base 0x{fault_module['base']:x})"
        )
        pdb = fault_module.get("pdb")
        if pdb and pdb.get("path"):
            pdb_path = Path(pdb["path"])
            candidates = [pdb_path]
            if not pdb_path.is_absolute():
                if executable:
                    candidates.append(executable.parent / pdb_path.name)
                candidates.append(game_dir / pdb_path.name)
            found_pdb = next((candidate for candidate in candidates if candidate.exists()), None)
            lines.append(f"- Embedded PDB reference: {pdb['path']}")
            lines.append(f"- Matching PDB found locally: {found_pdb if found_pdb else 'no'}")
            if pdb.get("guid"):
                lines.append(f"- PDB identity: {pdb['guid']} age {pdb.get('age')}")
    else:
        lines.append("- Faulting module: not found in the minidump module list")

    if exception["information"]:
        lines.append(f"- Access kind: {exception['information'][0]} (0=read, 1=write, 8=execute)")
    if len(exception["information"]) > 1:
        lines.append(f"- Access address: 0x{exception['information'][1]:016x}")
    lines.extend([
        f"- Minidump streams: {len(dump['streams'])}; captured memory ranges: "
        f"{len(dump['memory_ranges'])}",
        "",
        "### Registers",
        "",
    ])
    for name in REGISTERS_X64:
        if name in registers:
            lines.append(f"- {name}: 0x{registers[name]:016x}")

    lines.extend(["", "### Captured-memory limits", ""])
    zero_registers = [name for name, value in registers.items() if value == 0]
    if zero_registers:
        lines.append(
            "- Zero-valued registers: " + ", ".join(zero_registers)
            + " (the value alone does not mean every register is a pointer)."
        )
    pointer_registers = ("Rcx", "Rdx", "Rsi", "Rdi", "R8", "R9", "R12", "R13", "R14", "R15")
    absent = []
    for name in pointer_registers:
        address = registers.get(name)
        if address and address >= 0x10000 and dump["memory_reader"](address, 1) is None:
            absent.append(f"{name}=0x{address:016x}")
    if absent:
        lines.append("- Uncaptured non-null register addresses: " + ", ".join(absent))
        lines.append("- These object/heap bytes were not captured; the dump cannot show what the pointer referred to.")
    else:
        lines.append("- None of the checked non-null registers point outside captured memory.")

    lines.extend(["", "## Loaded modules relevant to mod loaders", ""])
    noteworthy = []
    for module in dump["modules"]:
        lower = module["name"].lower().replace("/", "\\")
        base_name = Path(module["name"]).name.lower()
        if any(token in lower for token in ("cps.dll", "ue4ss.dll", "winmm.dll", "dwmapi.dll")):
            noteworthy.append((base_name, module["name"]))
    if noteworthy:
        for base_name, name in noteworthy:
            note = ""
            if base_name == "dwmapi.dll" and "\\windows\\system32\\" in name.lower().replace("/", "\\"):
                note = " (Windows system DLL, not the UE4SS proxy)"
            lines.append(f"- {name}{note}")
    else:
        lines.append("- No CPS/UE4SS/winmm/dwmapi modules found.")
    lines.append(f"- Total loaded modules recorded: {len(dump['modules'])}")

    lines.extend(["", "## CrashContext stack", ""])
    if xml["stack"]:
        for index, frame in enumerate(xml["stack"]):
            address = frame["base"] + frame["offset"]
            module = find_module(dump["modules"], address)
            if module:
                rendered = (
                    f"{module['name']} + 0x{address - module['base']:x} "
                    f"(0x{address:016x})"
                )
            else:
                rendered = (
                    f"{frame['reported_module']} + 0x{frame['offset']:x} "
                    f"(0x{address:016x})"
                )
            lines.append(f"{index:02d}. {rendered}")
    else:
        lines.append("- CrashContext contains no PCallStack frames.")

    if executable:
        lines.extend(["", "## Disassembly around the faulting instruction", ""])
        lines.append(f"- Executable: {executable}")
        excerpt = disassemble(objdump, executable, rip)
        if excerpt:
            lines.append("")
            lines.extend(f"    {line}" for line in excerpt)
        else:
            lines.append("- No disassembly available; install/find GNU objdump or pass --objdump.")
    else:
        lines.extend([
            "",
            "## Disassembly",
            "",
            "- Executable not found. Pass --exe or --game-dir to locate SB-Win64-Shipping.exe.",
        ])

    lines.extend(["", "## Vortex state near this crash", ""])
    deployment = vortex_activity.get("deploy")
    launch = vortex_activity.get("launch")
    if deployment:
        lines.append(
            f"- Last Vortex deployment before the crash: {format_time(deployment['time'])}; "
            f"{deployment.get('enabled_mod_count', 'unknown')} enabled mods, "
            f"{deployment.get('file_count', 'unknown')} files, "
            f"{deployment.get('deployment_method', 'unknown')}."
        )
    else:
        lines.append("- No Vortex deployment event was found before this crash.")
    if launch:
        lines.append(
            f"- Last Vortex launch marker: {format_time(launch['time'])}; "
            f"{launch['enabled_mod_count']} enabled mods."
        )

    if vortex_manifest.get("error"):
        lines.append(f"- Vortex manifest: {vortex_manifest['error']}")
    elif vortex_manifest:
        lines.append(
            f"- Current deployment manifest: {format_time(vortex_manifest.get('time'))}; "
            f"{len(vortex_manifest.get('sources', []))} mod sources, "
            f"{vortex_manifest.get('file_count', 0)} files."
        )
        if vortex_manifest.get("time") and vortex_manifest["time"] > crash_time:
            lines.append("- Warning: this manifest was written after the crash, so it is not a historical snapshot.")
        sources = vortex_manifest.get("sources", [])
        for query in mod_matches:
            matched = [source for source in sources if query.casefold() in source.casefold()]
            if matched:
                lines.append(f"- Current manifest matches for {query!r}: " + "; ".join(matched))
            else:
                lines.append(f"- Current manifest has no match for {query!r}.")
    else:
        lines.append("- No Vortex deployment manifest found.")

    lines.extend([
        "",
        "## Limits",
        "",
        "- This report reads the dump; it does not change or enlarge it.",
        "- Exact C++ function names require matching PDB symbols. A missing heap range cannot be reconstructed after the crash.",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyze the newest UE4 crash minidump or a specified crash folder."
    )
    parser.add_argument(
        "crash",
        nargs="?",
        help="Crash folder or UE4Minidump.dmp. Defaults to the latest folder under LOCALAPPDATA\\SB\\Saved\\Crashes.",
    )
    parser.add_argument(
        "--game-dir",
        type=Path,
        default=Path(r"C:\Program Files (x86)\Steam\steamapps\common\StellarBlade"),
        help="Stellar Blade install folder.",
    )
    parser.add_argument("--exe", type=Path, help="Path to SB-Win64-Shipping.exe.")
    parser.add_argument("--objdump", help="objdump executable; defaults to objdump from PATH.")
    parser.add_argument(
        "--mod-match",
        action="append",
        default=[],
        help="Search this text in the current Vortex deployment manifest; repeatable.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write the report here. Defaults to a unique CrashAnalysis file beside the dump.",
    )
    args = parser.parse_args()

    try:
        crash_dir = discover_crash_path(args.crash)
        game_dir = args.game_dir.expanduser()
        report = build_report(
            crash_dir, game_dir, args.exe.expanduser() if args.exe else None,
            args.objdump or shutil.which("objdump"), args.mod_match,
        )
        dump_path = crash_dir if crash_dir.is_file() else crash_dir / "UE4Minidump.dmp"
        if dump_path.is_file():
            crash_dir = dump_path.parent
        output_path = args.output.expanduser() if args.output else None
        if output_path is None:
            context = read_xml_context(crash_dir)["values"]
            stamp = dt.datetime.fromtimestamp(dump_path.stat().st_mtime).astimezone().strftime("%Y%m%d-%H%M%S")
            signature = context.get("PCallStackHash", "unknown")[:8]
            output_path = crash_dir / f"CrashAnalysis-{stamp}-{signature}.md"
            suffix = 2
            while output_path.exists():
                output_path = crash_dir / f"CrashAnalysis-{stamp}-{signature}-{suffix}.md"
                suffix += 1
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text(report, encoding="utf-8")
        print(report)
        print(f"\nReport written to: {output_path}")
        return 0
    except (OSError, ValueError, ET.ParseError, struct.error) as error:
        print(f"Crash report analysis failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
