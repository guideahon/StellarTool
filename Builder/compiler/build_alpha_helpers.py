"""build_alpha_helpers - empaqueta las ALPHA del helper vanilla (sin CNS).

Cada ALPHA es el MISMO main.lua con un config.lua distinto (clave `strategy`),
para que el tester instale una a la vez y el log diga cual corrio. La tabla de
builds vive en vanilla_helper.py, que es la misma que usa el Builder de Stellar
Tool, asi los zips sueltos y lo que compila la app no se desincronizan.

Salida: Release/ALPHA/StellarSouls-VanillaOutfitRestore-ALPHA<n>-<slug>.zip
Cada zip trae en la raiz:
    README-ALPHA.txt
    StellarSoulsVanillaRestore/            <- copiar a ue4ss\\Mods

Uso:
    python Builder/compiler/build_alpha_helpers.py
"""
from __future__ import annotations

import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vanilla_helper import ALPHA_IDS, ALPHAS, MOD_NAME, build_name, compile_vanilla_helper

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Release" / "ALPHA"

README_TEMPLATE = """Stellar Souls - Vanilla Outfit Restore
{title}
Build: {build}

ALPHA TEST BUILD. Not a release. It is safe to delete at any time.

WHAT IT IS
This is the outfit-restore helper WITHOUT CNS. It exists for the case where the
shield-break Skin Suit does not come off, most often after a boss QTE: your
shield repairs while you have no control, nothing re-evaluates your outfit, and
EVE stays in the Skin Suit until something forces a repaint.

The helper only acts when EVE is wearing the Skin Suit MESH while the Skin Suit
EQUIPMENT is already gone. While your shield is really broken, the equipment is
present, so it cannot take the Skin Suit away from you.

WHAT THIS PARTICULAR BUILD DOES
{note}

REQUIREMENTS
UE4SS for Stellar Blade (the Chrisr0 build):
    https://github.com/Chrisr0/RE-UE4SS/releases
Copy its contents into StellarBlade/SB/Binaries/Win64
CNS is NOT required.

INSTALL
1. Delete any previous {mod} folder from
       StellarBlade/SB/Binaries/Win64/ue4ss/Mods
2. Copy the {mod} folder from this zip into that Mods folder.
3. Open StellarBlade/SB/Binaries/Win64/ue4ss/mods.txt and add this line near the
   top (once, it works for every ALPHA):
       {mod} : 1
4. Start the game.

CHECK IT LOADED
A log is written to:
    %USERPROFILE%\\StellarSoulsVanillaRestore.log
On Proton/Wine that is inside the prefix, e.g.
    .../pfx/drive_c/users/steamuser/StellarSoulsVanillaRestore.log
The first lines say "Loaded." and print a DIAGNOSTIC block.
If that file never appears, UE4SS itself is not loading and no ALPHA can work.

HOTKEYS
    Alt+R   restore now (forces this build's strategy)
    Alt+D   write a diagnostic block to the log

SETTINGS
{mod}/Scripts/config.lua
Most useful key: suitId. Leave it empty for auto-detect. If the log never says
"Remembered worn suit id", set it by hand, e.g.
    suitId = "BS_42",
Suit id list: https://pastebin.com/jrTJVcde

WHAT TO REPORT BACK
1. Which ALPHA number you ran.
2. Whether Alt+R repaints EVE while she is stuck in the Skin Suit.
3. The log file (or the lines around STUCK-GUARD / Restore result).
"""


def build_one(build_id: str, staging: Path) -> Path:
    spec = ALPHAS[build_id]
    name = build_name(build_id)
    work = staging / name
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)

    compile_vanilla_helper(build_id, work)
    (work / "README-ALPHA.txt").write_text(
        README_TEMPLATE.format(title=spec["title"], build=name, note=spec["note"], mod=MOD_NAME),
        encoding="utf-8")

    OUT.mkdir(parents=True, exist_ok=True)
    zip_path = OUT / f"StellarSouls-VanillaOutfitRestore-{name}.zip"
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for path in sorted(work.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(work).as_posix())
    return zip_path


def main() -> int:
    staging = OUT / "_staging"
    staging.mkdir(parents=True, exist_ok=True)
    made = [build_one(build_id, staging) for build_id in ALPHA_IDS]
    shutil.rmtree(staging, ignore_errors=True)
    for z in made:
        print(f"OK {z.relative_to(ROOT)}  ({z.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
