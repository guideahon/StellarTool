"""toml_patch - patches declarativos TOML de propiedades .uasset (opcional).

Formato (estilo automod / jpabscale, credito en Shoutouts): un archivo por tabla,
nombrado <Tabla>.toml, con secciones por fila:

    [Player]
    MaxBurstGauge = 1800
    AttackSpeed = 1.3

    [M_HedgeBoarBrute]
    MaxHP = 120000

Aplica sobre el JSON UAssetAPI en memoria (apply_toml_to_doc) o sobre uassets ya
staged via tojson->apply->fromjson (apply_toml_dir). Solo modifica propiedades
EXISTENTES (no crea filas). Valores string se agregan al NameMap por las dudas.

Requiere Python 3.11+ (tomllib). El Python embebido del tool es 3.12 -> OK.
"""
from __future__ import annotations

from pathlib import Path

try:
    import tomllib
except ModuleNotFoundError:  # py<3.11
    tomllib = None


def _rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def _prop(row, name):
    return next((p for p in row["Value"] if p["Name"] == name), None)


def _addnames(doc, names):
    nm = doc.get("NameMap")
    if nm is None:
        return
    have = set(nm)
    for n in names:
        if isinstance(n, str) and n and n not in have:
            have.add(n)
            nm.append(n)


def apply_toml_to_doc(doc: dict, table_patch: dict) -> int:
    """table_patch = {RowName: {Prop: value}}. Devuelve # de props aplicadas."""
    index = {r.get("Name"): r for r in _rows(doc)}
    applied = 0
    new_names = []
    for row_name, props in table_patch.items():
        row = index.get(row_name)
        if not row:
            continue
        pmap = {p["Name"]: p for p in row["Value"]}
        for prop_name, value in props.items():
            p = pmap.get(prop_name)
            if p is None:
                continue
            p["Value"] = value
            p["IsZero"] = value in (0, 0.0, False, None)
            applied += 1
            if isinstance(value, str):
                new_names.append(value)
    _addnames(doc, new_names)
    return applied


def load_toml(path: Path) -> dict:
    if tomllib is None:
        raise RuntimeError("tomllib no disponible (requiere Python 3.11+)")
    with open(path, "rb") as f:
        return tomllib.load(f)


def table_of(toml_path: Path) -> str:
    """Nombre de tabla desde el filename (<Tabla>.toml)."""
    return Path(toml_path).stem


def apply_toml_dir(patch_dir, staged_data_dir, tools_dir=None) -> dict:
    """Aplica cada <Tabla>.toml a staged_data_dir/<Tabla>.uasset via
    tojson->apply->fromjson. Devuelve {tabla: propsAplicadas}."""
    import toolchain
    patch_dir = Path(patch_dir)
    data = Path(staged_data_dir)
    report = {}
    for toml_file in sorted(patch_dir.glob("*.toml")):
        table = table_of(toml_file)
        uasset = data / f"{table}.uasset"
        if not uasset.exists():
            report[table] = "skip (no en el pak)"
            continue
        patch = load_toml(toml_file)
        counted = {}
        # edit_uasset verifica que fromjson haya reescrito el uasset: antes un
        # patch podia perderse en silencio y el pak salia con la tabla sin tocar.
        toolchain.edit_uasset(uasset, [
            lambda doc: counted.update(applied=apply_toml_to_doc(doc, patch))
        ])
        report[table] = counted.get("applied", 0)
    return report


if __name__ == "__main__":
    import argparse
    import json
    ap = argparse.ArgumentParser(description="Aplica patches TOML a uassets staged.")
    ap.add_argument("--patches", required=True, help="carpeta con <Tabla>.toml")
    ap.add_argument("--data", required=True, help="carpeta con <Tabla>.uasset")
    args = ap.parse_args()
    print(json.dumps(apply_toml_dir(args.patches, args.data), indent=2, ensure_ascii=False))
