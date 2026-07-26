"""vendor — empaqueta todas las fuentes del Builder en Builder/vendor/ para
distribuir la app a otra maquina (sin depender de C:\\Temp ni rutas del autor).

Copia: tablas base JSON (ssmod), toolchain (retoc/UAssetGUI/repak/usmap) y los
stagings legacy (mini-boss v131, First Run v14). Luego escribe vendor/paths.json
con rutas relativas. En runtime, si existe Builder/vendor/, tiene prioridad sobre
paths.json (ver _PATHS loader).

Uso:
    python vendor.py            # copia todo a Builder/vendor/
    python vendor.py --check    # solo lista tamanos, no copia

Advertencia: pesado (EffectTable ~262MB x varias copias). Considerar comprimir
para el release y descomprimir en primer arranque.
"""
from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path

BUILDER = Path(__file__).resolve().parent
VENDOR = BUILDER / "vendor"
PATHS = json.loads((BUILDER / "base_tables" / "paths.json").read_text(encoding="utf-8"))

# JSONs de ssmod que el compilador usa (sources.json + miniboss + reverts).
SSMOD_FILES = [
    "combatCT.json", "EventSpawnTable.json", "SkillTable_v.json",
    "DR_EffectTable.json", "VANILLA_EffectTable.json", "combatSK.json",
    "CharacterTable_sub.json", "SkillResultTable.json",
]
# NOTA: oo2core_9_win64.dll (Oodle) NO se vendoriza — es propietario, no se
# redistribuye. toolchain.ensure_oodle() lo toma del juego del usuario en runtime.
TOOL_FILES = ["retoc.exe", "UAssetGUI.exe", "repak.exe", "cue4parse.exe",
              "StellarBlade.usmap"]


def _sz(p: Path) -> int:
    return sum(f.stat().st_size for f in p.rglob("*") if f.is_file()) if p.is_dir() else (p.stat().st_size if p.exists() else 0)


def copy_all(check=False):
    ssmod = Path(PATHS["ssmodTables"]["path"])
    tools = Path(PATHS["tools"]["path"])
    mb_stage = Path(PATHS["stagings"]["miniBoss"])
    fr_stage = Path(PATHS["stagings"]["firstRun"])

    plan = []
    for f in SSMOD_FILES:
        plan.append((ssmod / f, VENDOR / "ssmod" / f))
    # SkillTable full (combate) desde BuildOutput
    src_sk = BUILDER.parent / "Development" / "BuildOutput" / "Stellar Souls (Combat Tweaks) 1.2.39" / "json" / "SkillTable.json"
    plan.append((src_sk, VENDOR / "ssmod" / "SkillTable_full.json"))
    for f in TOOL_FILES:
        plan.append((tools / f, VENDOR / "tools" / f))
    plan.append((mb_stage, VENDOR / "stagings" / "miniBoss"))
    plan.append((fr_stage, VENDOR / "stagings" / "firstRun"))
    # Helper base (StellarSoulsOutfitRestore) para compilar/instalar sin Stellar Souls.
    helper_src = Path(r"C:\Users\cristian\Documents\Stellar Souls\Release\Helper - Random CNS and 30s Random - 1.2.17\StellarSoulsOutfitRestore")
    plan.append((helper_src, VENDOR / "helper" / "StellarSoulsOutfitRestore"))

    total = 0
    for src, dst in plan:
        s = _sz(src)
        total += s
        mark = "OK " if src.exists() else "MISS"
        print(f"{mark} {s/1e6:8.1f} MB  {src} -> {dst}")
        if not check and src.exists():
            dst.parent.mkdir(parents=True, exist_ok=True)
            if src.is_dir():
                shutil.copytree(src, dst, dirs_exist_ok=True)
            else:
                shutil.copy2(src, dst)
    print(f"\nTOTAL ~{total/1e6:.0f} MB")

    if not check:
        vendor_paths = {
            "ssmodTables": {"path": "vendor/ssmod", "env": "SSMOD_TABLES"},
            "tools": {"path": "vendor/tools", "env": "STELLAR_TOOLS"},
            "stagings": {"env": "SSMOD_STAGINGS",
                         "miniBoss": "vendor/stagings/miniBoss",
                         "firstRun": "vendor/stagings/firstRun"},
            "relativeTo": "Builder",
        }
        (VENDOR / "paths.json").write_text(json.dumps(vendor_paths, indent=1), encoding="utf-8")
        print(f"wrote {VENDOR / 'paths.json'}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Vendoriza las fuentes del Builder.")
    ap.add_argument("--check", action="store_true", help="solo listar tamanos")
    args = ap.parse_args()
    copy_all(check=args.check)
