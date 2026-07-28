"""vanilla_helper - helper de restauracion de outfit SIN CNS (builds ALPHA).

El helper CNS (StellarSoulsOutfitRestore) repinta llamando
BP_CNS_SaveData_C:ReloadDataFromLastSave(). Sin CNS ese objeto no existe, asi
que estas builds ALPHA prueban distintos caminos VANILLA para el mismo repaint.
Cada ALPHA es el MISMO main.lua con otro `strategy` en config.lua: se instala
UNA a la vez y el log dice cual corrio.

Usado por build_custom (para meterlo en el ZIP/instalacion del Builder) y por
build_alpha_helpers (para empaquetar los zips sueltos de test).
"""
from __future__ import annotations

import shutil
from pathlib import Path

from helper_compiler import apply_overrides

BUILDER_DIR = Path(__file__).resolve().parent.parent
SRC = BUILDER_DIR / "vendor" / "helper" / "StellarSoulsVanillaRestore"
MOD_NAME = "StellarSoulsVanillaRestore"

# id -> spec. `order` fija el orden en la UI; `overrides` van a config.lua.
ALPHAS = {
    "alpha1": {
        "order": 1,
        "slug": "probe",
        "title": "Probe / diagnostics (writes nothing)",
        "note": ("Reads only. Proves UE4SS loads and dumps mesh / equipment / cheat-manager\n"
                 "state to the log. Run this one FIRST. It never changes your outfit."),
        "overrides": {
            "strategy": "probe",
            "enableStuckGuard": False,
            "enableSignalEdgeRestore": False,
            "enableHeartbeat": True,
        },
    },
    "alpha2": {
        "order": 2,
        "slug": "meshrepaint",
        "title": "Repaint via ApplyMeshInfo (no cheat manager)",
        "note": ("Asks the character to re-apply its own mesh. Needs no cheat manager and no\n"
                 "suit id, so it is the most likely one to work on a plain install."),
        "overrides": {"strategy": "meshRepaint"},
    },
    "alpha3": {
        "order": 3,
        "slug": "cheatequip",
        "title": "SBPlayerEquipItem, existing cheat manager",
        "note": ("Re-equips your worn suit through SBCheatManager. Requires\n"
                 "'CheatManagerEnablerMod : 1' in ue4ss\\mods.txt (it ships with UE4SS)."),
        "overrides": {"strategy": "cheatEquip", "allowCheatManagerConstruct": False},
    },
    "alpha4": {
        "order": 4,
        "slug": "cheatequip-construct",
        "title": "SBPlayerEquipItem, builds its own cheat manager",
        "note": ("Same as ALPHA3, but builds its own cheat manager if the game does not have\n"
                 "one. Use this if ALPHA3 logs 'No SBCheatManager'."),
        "overrides": {"strategy": "cheatEquip", "allowCheatManagerConstruct": True},
    },
    "alpha5": {
        "order": 5,
        "slug": "equiptoggle",
        "title": "Unequip + re-equip (mimics the manual fix)",
        "note": ("Unequips and re-equips the suit, which is exactly the manual fix, done for\n"
                 "you. Use it if ALPHA4 equips but EVE still does not repaint."),
        "overrides": {"strategy": "equipToggle", "allowCheatManagerConstruct": True},
    },
    "alpha6": {
        "order": 6,
        "slug": "chain",
        "title": "Chain: repaint -> equip -> toggle",
        "note": ("Tries every strategy in order and stops at the first one that repaints EVE.\n"
                 "Use this if you do not want to test one by one; the log names the winner."),
        "overrides": {"strategy": "chain", "allowCheatManagerConstruct": True},
    },
}

ALPHA_IDS = [k for k, _ in sorted(ALPHAS.items(), key=lambda kv: kv[1]["order"])]


def build_name(build_id: str) -> str:
    spec = ALPHAS[build_id]
    return f"ALPHA{spec['order']}-{spec['slug']}"


def is_enabled(value) -> bool:
    """True si la respuesta pide una ALPHA ('off'/''/None = no)."""
    return bool(value) and str(value).lower() not in ("off", "none", "no", "false")


def compile_vanilla_helper(build_id: str, out_mods_dir: Path) -> Path:
    """Escribe <out_mods_dir>/StellarSoulsVanillaRestore listo para ue4ss\\Mods."""
    if build_id not in ALPHAS:
        raise KeyError(f"ALPHA desconocida: {build_id} (validas: {ALPHA_IDS})")
    out_mods_dir = Path(out_mods_dir)
    root = out_mods_dir / MOD_NAME
    scripts = root / "Scripts"
    if root.exists():
        shutil.rmtree(root)
    scripts.mkdir(parents=True)

    shutil.copy2(SRC / "Scripts" / "main.lua", scripts / "main.lua")
    config_text = (SRC / "Scripts" / "config.lua").read_text(encoding="utf-8")
    overrides = dict(ALPHAS[build_id]["overrides"], buildName=build_name(build_id))
    (scripts / "config.lua").write_text(apply_overrides(config_text, overrides), encoding="utf-8")
    (root / "enabled.txt").write_text("", encoding="utf-8")
    return root


def install_note(build_id: str) -> str:
    """Bloque para la guia de instalacion del ZIP (ingles, igual que el README)."""
    spec = ALPHAS[build_id]
    return (
        f"\n\nVANILLA OUTFIT RESTORE - {build_name(build_id)} (ALPHA TEST BUILD)\n"
        "This is an experimental helper for people who do NOT use CNS. It is not a\n"
        "release: it exists to find out which vanilla repaint path works.\n\n"
        f"{spec['note']}\n\n"
        "Requires UE4SS (https://github.com/Chrisr0/RE-UE4SS/releases), NOT CNS.\n"
        f"Copy ue4ss\\Mods\\{MOD_NAME} to\n"
        f"   StellarBlade\\SB\\Binaries\\Win64\\ue4ss\\Mods\\{MOD_NAME}\n"
        f"and add '{MOD_NAME} : 1' near the top of ue4ss\\mods.txt.\n"
        "Log: %USERPROFILE%\\StellarSoulsVanillaRestore.log\n"
        "Hotkeys: Alt+R restore now, Alt+D diagnostic dump.\n"
        "Install only ONE ALPHA at a time (they share the folder name).\n"
    )
