"""Genera un catálogo granular de variantes de moveset.

El catálogo se construye desde los paks proporcionados por el usuario y una
instalación vanilla del juego. No modifica ninguna fuente. Las tablas se leen
con CUE4Parse porque las variantes son IoStore/Zen; los assets se enumeran
desempaquetando cada .utoc en un directorio temporal.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import toolchain

TABLES = (
    "SkillActiveStepTable", "SkillResultTable", "SkillTable",
    "SkillCommandTable", "CharacterMoveTable", "ProjectileTable",
)
FAMILIES = {"fusion", "scarlet", "raven"}
TIERS = {"queen", "goddess", "godqueen", "godempress"}
ASSET_EXTENSIONS = {".uasset", ".uexp", ".ubulk", ".umap", ".pak", ".ucas", ".utoc"}


def _variant_meta(directory: Path) -> dict | None:
    raw = directory.name.strip().lower().replace(" ", "-")
    parts = [p for p in re.split(r"[-_]+", raw) if p]
    aggro = bool(parts and parts[0] == "aggro")
    family_index = 1 if aggro else 0
    if family_index >= len(parts) or parts[family_index] not in FAMILIES:
        return None
    family = parts[family_index]
    tier = next((p for p in parts[family_index + 1:] if p in TIERS), "default")
    containers = {p.suffix.lower() for p in directory.iterdir() if p.is_file()}
    if not {".pak", ".ucas", ".utoc"}.issubset(containers):
        return None
    return {
        "id": raw,
        "family": family,
        "tier": tier,
        "aggro": aggro,
        "sourceDir": str(directory.resolve()),
        "containers": [str(p.resolve()) for p in sorted(directory.iterdir())
                       if p.is_file() and p.suffix.lower() in {".pak", ".ucas", ".utoc"}],
    }


def discover_variants(source: Path) -> list[dict]:
    found = []
    for directory in sorted(p for p in source.rglob("*") if p.is_dir()):
        meta = _variant_meta(directory)
        if meta:
            found.append(meta)
    unique = {item["id"]: item for item in found}
    return [unique[key] for key in sorted(unique)]


def _link_or_copy(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        os.link(source, target)
    except OSError:
        shutil.copy2(source, target)


def _game_root_files(game: Path, include_chunks: bool = True) -> list[Path]:
    paks = game / "SB" / "Content" / "Paks"
    return [p for p in paks.iterdir() if p.is_file()
            and (p.name.lower().startswith("global")
                 or (include_chunks and p.name.lower().startswith("pakchunk")))]


def _stage_for_variant(game: Path, meta: dict, root: Path) -> Path:
    stage = root / "stage"
    stage.mkdir(parents=True)
    # Solo los contenedores raíz del juego: ~mods no puede contaminar vanilla.
    # Los chunks ya contienen las tablas vanilla; para analizar una variante
    # se necesitan solo los globales como tipos estándar, evitando dos copias
    # de la misma tabla (vanilla + mod) en CUE4Parse.
    for source in _game_root_files(game, include_chunks=False):
        _link_or_copy(source, stage / source.name)
    for raw in meta["containers"]:
        source = Path(raw)
        _link_or_copy(source, stage / source.name)
    return stage


def _cue_export(input_dir: Path, output_dir: Path, patterns: list[str]) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    exe = toolchain.tools_dir() / "cue4parse.exe"
    mappings = toolchain.tools_dir() / "StellarBlade.usmap"
    args = [str(exe), "-i", str(input_dir), "-g", "GAME_UE4_26",
            "-f", "json", "-o", str(output_dir), "-y", "-m", str(mappings)]
    for pattern in patterns:
        args.extend(["-p", pattern])
    env = dict(os.environ)
    oodle = toolchain.oodle_dir()
    if oodle:
        env["PATH"] = str(oodle) + os.pathsep + env.get("PATH", "")
    result = subprocess.run(args, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", env=env,
                            cwd=str(oodle) if oodle else None, timeout=900)
    if result.returncode != 0:
        detail = (result.stdout + result.stderr)[-1200:]
        raise RuntimeError(f"CUE4Parse falló (rc={result.returncode}): {detail}")


def _load_cue(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(data, list) or not data or "Rows" not in data[0]:
        raise ValueError(f"Formato CUE4Parse inesperado: {path}")
    return data[0]["Rows"]


def _flatten(value, path=""):
    if isinstance(value, dict):
        for key, child in value.items():
            next_path = f"{path}.{key}" if path else key
            yield from _flatten(child, next_path)
    elif isinstance(value, list):
        # El array completo es una unidad de selección. Aplicar elementos
        # parciales sin una plantilla estable puede romper el round-trip.
        yield path, value
    else:
        yield path, value


def _change_value(value):
    if isinstance(value, str) and "::" in value:
        return value.rsplit("::", 1)[-1]
    return value


def _table_changes(table: str, before: dict, after: dict, variant: dict) -> list[dict]:
    changes = []
    before_names, after_names = set(before), set(after)
    for row in sorted(before_names | after_names):
        if row not in before:
            changes.append({
                "id": f"moveset.{variant['id']}.{table}.{row}.added",
                "variant": variant["id"], "family": variant["family"],
                "tier": variant["tier"], "aggro": variant["aggro"],
                "kind": "table", "table": table, "row": row,
                "propertyPath": "", "before": None, "after": after[row],
                "summary": f"Agrega la fila {row} en {table}",
                "conflictKey": f"table/{table}/{row}",
                "support": "needs_validation",
            })
            continue
        if row not in after:
            changes.append({
                "id": f"moveset.{variant['id']}.{table}.{row}.removed",
                "variant": variant["id"], "family": variant["family"],
                "tier": variant["tier"], "aggro": variant["aggro"],
                "kind": "table", "table": table, "row": row,
                "propertyPath": "", "before": before[row], "after": None,
                "summary": f"Quita la fila {row} de {table}",
                "conflictKey": f"table/{table}/{row}",
                "support": "needs_validation",
            })
            continue
        old = dict(_flatten(before[row]))
        new = dict(_flatten(after[row]))
        for prop in sorted(set(old) | set(new)):
            old_value, new_value = old.get(prop), new.get(prop)
            if old_value == new_value:
                continue
            support = "scalar" if not isinstance(new_value, (dict, list)) else "needs_validation"
            changes.append({
                "id": f"moveset.{variant['id']}.{table}.{row}.{prop}",
                "variant": variant["id"], "family": variant["family"],
                "tier": variant["tier"], "aggro": variant["aggro"],
                "kind": "table", "table": table, "row": row,
                "propertyPath": prop,
                "before": _change_value(old_value), "after": _change_value(new_value),
                "summary": f"{table} · {row} · {prop}",
                "conflictKey": f"table/{table}/{row}/{prop}",
                "support": support,
            })
    return changes


def _asset_changes(meta: dict, extracted: Path) -> list[dict]:
    content = extracted / "SB" / "Content"
    if not content.exists():
        return []
    result = []
    for asset in sorted(content.rglob("*")):
        if not asset.is_file() or asset.suffix.lower() not in ASSET_EXTENSIONS:
            continue
        game_path = str(asset.relative_to(extracted)).replace("\\", "/")
        if game_path.lower().endswith(tuple(f"{table}.uasset".lower() for table in TABLES)):
            continue
        result.append({
            "id": f"moveset.{meta['id']}.asset.{hashlib.sha1(game_path.encode()).hexdigest()[:12]}",
            "variant": meta["id"], "family": meta["family"],
            "tier": meta["tier"], "aggro": meta["aggro"],
            "kind": "asset", "gamePath": game_path,
            "summary": f"Incluye {game_path}",
            "conflictKey": f"asset/{game_path.lower()}",
            "support": "asset",
        })
    return result


def _fingerprint(meta: dict, game: Path) -> str:
    h = hashlib.sha256()
    h.update(str(game.resolve()).encode())
    h.update(meta["id"].encode())
    for raw in meta["containers"]:
        file = Path(raw)
        h.update(file.name.encode())
        h.update(str(file.stat().st_size).encode())
        h.update(str(file.stat().st_mtime_ns).encode())
    return h.hexdigest()


def build_catalog(source: Path, game: Path, output: Path) -> dict:
    variants = discover_variants(source)
    if not variants:
        raise RuntimeError("No se encontraron variantes completas (.pak/.ucas/.utoc).")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="stellartool_moveset_") as temp:
        temp_root = Path(temp)
        vanilla = temp_root / "vanilla"
        baseline = {}
        # Una corrida por tabla evita colisiones de nombres si el juego tiene
        # copias de la tabla en distintos contenedores.
        for table in TABLES:
            table_out = vanilla / table
            stage = temp_root / f"baseline_{table}"
            stage.mkdir()
            for file in _game_root_files(game, include_chunks=True):
                _link_or_copy(file, stage / file.name)
            _cue_export(stage, table_out, [f"*/{table}.uasset"])
            path = next(table_out.rglob(f"{table}.json"), None)
            if path:
                baseline[table] = _load_cue(path)

        catalog = {
            "schemaVersion": 1,
            "gameVersion": "GAME_UE4_26",
            "sourceDir": str(source.resolve()),
            "gameDir": str(game.resolve()),
            "variants": [], "changes": [],
        }
        for meta in variants:
            fingerprint = _fingerprint(meta, game)
            variant_root = temp_root / meta["id"]
            stage = _stage_for_variant(game, meta, variant_root)
            json_out = variant_root / "json"
            patterns = [f"*/{table}.uasset" for table in TABLES]
            _cue_export(stage, json_out, patterns)
            changes = []
            for table in TABLES:
                path = next(json_out.rglob(f"{table}.json"), None)
                if path and table in baseline:
                    changes.extend(_table_changes(table, baseline[table], _load_cue(path), meta))
            # retoc unpack es deliberadamente opcional: las tablas siguen siendo
            # analizables aunque un asset visual no sea convertible.
            extracted = variant_root / "assets"
            extracted.mkdir()
            utoc = next((Path(p) for p in meta["containers"] if p.lower().endswith(".utoc")), None)
            if utoc:
                retoc = toolchain.tools_dir() / "retoc.exe"
                cp = subprocess.run([str(retoc), "unpack", str(utoc), str(extracted)],
                                    capture_output=True, text=True, encoding="utf-8",
                                    errors="replace", timeout=900)
                if cp.returncode == 0:
                    changes.extend(_asset_changes(meta, extracted))
            meta = dict(meta)
            meta["fingerprint"] = fingerprint
            meta["changeCount"] = len(changes)
            catalog["variants"].append(meta)
            catalog["changes"].extend(changes)

        groups = {}
        for change in catalog["changes"]:
            groups.setdefault(change["conflictKey"], []).append(change["id"])
        for change in catalog["changes"]:
            candidates = groups[change["conflictKey"]]
            values = set()
            for candidate in catalog["changes"]:
                if candidate["id"] not in candidates:
                    continue
                # Las entradas visuales no tienen before/after de tabla: su
                # valor es el contenedor/variante que las aporta.
                value = candidate.get("after", candidate.get("variant"))
                values.add(json.dumps(value, sort_keys=True))
            change["conflict"] = len(values) > 1
            change["conflictGroup"] = change["conflictKey"] if change["conflict"] else ""
        catalog["summary"] = {
            "variants": len(catalog["variants"]),
            "changes": len(catalog["changes"]),
            "conflictGroups": len({c["conflictGroup"] for c in catalog["changes"] if c["conflictGroup"]}),
        }
        output.write_text(json.dumps(catalog, indent=2, ensure_ascii=False), encoding="utf-8")
        return catalog


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    catalog = build_catalog(args.source, args.game, args.out)
    print(json.dumps({"ok": True, "out": str(args.out), "summary": catalog["summary"]}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
