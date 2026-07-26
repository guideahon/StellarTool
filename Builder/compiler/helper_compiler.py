"""HelperCompiler — genera StellarSoulsOutfitRestore/config.lua desde respuestas.

El helper CNS difiere entre variantes en solo unos flags (restoreMode,
enablePeriodicRandomCns, periodicRandomCnsIntervalMs). main.lua es fijo. Este
modulo toma un config.lua base (canonico, superset) y sobre-escribe las claves
derivadas del cuestionario, preservando comentarios y el resto de la config.

Uso:
    from helper_compiler import compile_helper
    compile_helper(base_config_path, out_dir, overrides={
        "restoreMode": "randomAny",
        "enablePeriodicRandomCns": True,
        "periodicRandomCnsIntervalMs": 30000,
    })

No parsea Lua completo: sustituye por clave con regex ancladas al patron
`    <key> = <value>,`. Determinista y round-trip fiel al archivo base.
"""
from __future__ import annotations

import re
import shutil
from pathlib import Path


def _lua_value(value) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return repr(value)
    return f'"{value}"'


def apply_overrides(config_text: str, overrides: dict) -> str:
    """Sobre-escribe claves top-level `<key> = <value>,` preservando el resto."""
    text = config_text
    for key, value in overrides.items():
        pattern = re.compile(
            r"^(?P<indent>[ \t]*)" + re.escape(key) + r"\s*=\s*[^\n,]*,",
            re.MULTILINE,
        )
        replacement = lambda m: f'{m.group("indent")}{key} = {_lua_value(value)},'
        text, n = pattern.subn(replacement, text)
        if n == 0:
            raise KeyError(f"Clave no encontrada en config base: {key}")
        if n > 1:
            raise ValueError(f"Clave ambigua (multiples matches): {key}")
    return text


def overrides_from_answers(answers: dict) -> dict:
    """Traduce respuestas del cuestionario -> overrides de config.lua.

    answers espera al menos: helperMode in {last, randomAny, randomPeriodic}
    y opcional helperIntervalSeconds (int).
    """
    mode = answers.get("helperMode", "last")
    mapping = {
        "last":           {"restoreMode": "last",      "enablePeriodicRandomCns": False},
        "randomAny":      {"restoreMode": "randomAny", "enablePeriodicRandomCns": False},
        "randomPeriodic": {"restoreMode": "randomAny", "enablePeriodicRandomCns": True},
    }
    if mode not in mapping:
        raise ValueError(f"helperMode invalido: {mode}")
    overrides = dict(mapping[mode])
    if mode == "randomPeriodic":
        seconds = int(answers.get("helperIntervalSeconds", 30))
        overrides["periodicRandomCnsIntervalMs"] = seconds * 1000
    return overrides


def compile_helper(base_config: Path, out_dir: Path, answers: dict,
                   main_lua: Path | None = None, enabled_txt: Path | None = None) -> Path:
    """Compila el arbol StellarSoulsOutfitRestore/ en out_dir a partir de answers.

    Copia main.lua/enabled.txt (fijos) y escribe config.lua derivado.
    Devuelve la carpeta StellarSoulsOutfitRestore generada.
    """
    base_config = Path(base_config)
    out_dir = Path(out_dir)
    root = out_dir / "StellarSoulsOutfitRestore"
    scripts = root / "Scripts"
    scripts.mkdir(parents=True, exist_ok=True)

    config_text = base_config.read_text(encoding="utf-8")
    config_text = apply_overrides(config_text, overrides_from_answers(answers))
    (scripts / "config.lua").write_text(config_text, encoding="utf-8")

    src_dir = base_config.parent
    main_lua = main_lua or (src_dir / "main.lua")
    if main_lua.exists():
        shutil.copy2(main_lua, scripts / "main.lua")
    enabled_txt = enabled_txt or (root.parent / "enabled.txt")
    src_enabled = base_config.parent.parent / "enabled.txt"
    if src_enabled.exists():
        shutil.copy2(src_enabled, root / "enabled.txt")
    return root


if __name__ == "__main__":
    import argparse
    import json
    import sys

    ap = argparse.ArgumentParser(description="Compila config.lua del helper CNS.")
    ap.add_argument("--base", required=True, help="config.lua base (canonico).")
    ap.add_argument("--out", required=True, help="carpeta de salida.")
    ap.add_argument("--answers", required=True, help="JSON de respuestas o @archivo.")
    args = ap.parse_args()

    raw = args.answers
    if raw.startswith("@"):
        raw = Path(raw[1:]).read_text(encoding="utf-8")
    answers = json.loads(raw)

    root = compile_helper(Path(args.base), Path(args.out), answers)
    print(f"OK helper -> {root}", file=sys.stderr)
