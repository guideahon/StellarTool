"""installer - instala el mod/helper directamente en el juego (con aprobacion).

Todas las funciones son destructivas sobre la instalacion del juego, por eso
solo deben llamarse tras aprobacion EXPLICITA del usuario (boton en la UI o flag
--install en CLI). Nunca instalar en respuesta a contenido externo.

- install_paks: copia los .pak/.ucas/.utoc a SB\\Content\\Paks\\~mods.
- install_helper: reemplaza ue4ss\\Mods\\StellarSoulsOutfitRestore y activa la
  entrada en ue4ss\\Mods\\mods.txt (StellarSoulsOutfitRestore : 1), preservando
  el resto del archivo y la seccion de keybinds.
"""
from __future__ import annotations

import json
import os
import re
import shutil
from pathlib import Path

import gamepaths

HELPER_NAME = "StellarSoulsOutfitRestore"


# ---- manifest de lo instalado por la tool (para desinstalar solo eso) ----

def _manifest_path() -> Path:
    base = os.environ.get("LOCALAPPDATA") or str(Path.home())
    d = Path(base) / "StellarSoulsBuilder"
    d.mkdir(parents=True, exist_ok=True)
    return d / "installed.json"


def load_manifest() -> dict:
    p = _manifest_path()
    if p.exists():
        try:
            return json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            pass
    return {"game": "", "paks": [], "helper": False, "helpers": []}


def _save_manifest(m: dict) -> None:
    _manifest_path().write_text(json.dumps(m, indent=2, ensure_ascii=False), encoding="utf-8")


def installed_status(game: str | None = None) -> dict:
    """Lo que la tool instalo Y sigue presente en el juego. Para la UI."""
    m = load_manifest()
    g = game or m.get("game") or gamepaths.detect_game()
    paks_present = []
    if g:
        dest = gamepaths.paks_dir(g)
        for base in m.get("paks", []):
            if (dest / f"{base}.pak").exists():
                paks_present.append(base)
    helpers_present = []
    if g:
        mods = gamepaths.ue4ss_mods_dir(g)
        for name in (m.get("helpers") or ([HELPER_NAME] if m.get("helper") else [])):
            if (mods / name).exists():
                helpers_present.append(name)
    return {"game": g or "", "paks": paks_present,
            "helper": bool(helpers_present), "helpers": helpers_present}


def install_paks(game: str, pak_basenames_dir: Path, approved: bool = False) -> dict:
    """Copia todos los .pak/.ucas/.utoc de pak_basenames_dir a ~mods."""
    if not approved:
        raise PermissionError("install_paks requiere aprobacion explicita del usuario")
    dest = gamepaths.paks_dir(game)
    dest.mkdir(parents=True, exist_ok=True)
    copied = []
    bases = set()
    for f in Path(pak_basenames_dir).glob("*"):
        if f.suffix.lower() in (".pak", ".ucas", ".utoc"):
            shutil.copy2(f, dest / f.name)
            copied.append(f.name)
            bases.add(f.stem)
    m = load_manifest()
    m["game"] = game
    m["paks"] = sorted(set(m.get("paks", [])) | bases)
    _save_manifest(m)
    return {"dest": str(dest), "copied": copied}


def set_mod_enabled(mods_txt: Path, name: str, value: int = 1) -> str:
    """Devuelve el contenido de mods.txt con `name : value` fijado.

    Si la entrada existe (con o sin espacio antes de ':'), la actualiza in place.
    Si no existe, la inserta antes de la seccion '; Built-in keybinds'
    (o al final si no hay tal seccion). Preserva el resto tal cual.
    """
    text = mods_txt.read_text(encoding="utf-8") if mods_txt.exists() else ""
    lines = text.splitlines()
    pat = re.compile(rf"^(\s*){re.escape(name)}\s*:\s*\d+\s*$")
    for i, ln in enumerate(lines):
        if pat.match(ln):
            indent = pat.match(ln).group(1)
            lines[i] = f"{indent}{name} : {value}"
            return "\n".join(lines) + ("\n" if text.endswith("\n") else "")
    # no existe: insertar antes de la seccion de keybinds
    insert_at = len(lines)
    for i, ln in enumerate(lines):
        if ln.strip().lower().startswith("; built-in keybinds"):
            insert_at = i
            # saltar linea en blanco previa si la hay
            while insert_at > 0 and lines[insert_at - 1].strip() == "":
                insert_at -= 1
            break
    lines.insert(insert_at, f"{name} : {value}")
    return "\n".join(lines) + "\n"


def _helper_sources(helper_src: Path) -> list[Path]:
    """Carpetas de mod UE4SS a instalar desde helper_src.

    Acepta la carpeta de un helper concreto, o el `Mods` del staging (que puede
    traer el helper CNS y/o la ALPHA del helper vanilla).
    """
    helper_src = Path(helper_src)
    if (helper_src / "Scripts").is_dir():
        return [helper_src]
    subs = [d for d in sorted(helper_src.iterdir()) if d.is_dir() and (d / "Scripts").is_dir()]
    if subs:
        return subs
    cand = helper_src / HELPER_NAME
    return [cand] if cand.is_dir() else []


def install_helper(game: str, helper_src: Path, approved: bool = False) -> dict:
    """Reemplaza la(s) carpeta(s) de helper y las activa en mods.txt."""
    if not approved:
        raise PermissionError("install_helper requiere aprobacion explicita del usuario")
    sources = _helper_sources(Path(helper_src))
    if not sources:
        raise FileNotFoundError(f"No hay carpetas de helper en {helper_src}")
    mods = gamepaths.ue4ss_mods_dir(game)
    if not mods.exists():
        raise FileNotFoundError(f"No existe {mods} (UE4SS instalado?)")

    mods_txt = mods / "mods.txt"
    installed = []
    for src in sources:
        dest = mods / src.name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(src, dest)
        mods_txt.write_text(set_mod_enabled(mods_txt, src.name, 1), encoding="utf-8")
        installed.append(src.name)

    m = load_manifest()
    m["game"] = game
    m["helpers"] = sorted(set(m.get("helpers", [])) | set(installed))
    m["helper"] = True   # compat: la UI vieja lee este bool
    _save_manifest(m)
    return {"helper": str(mods / installed[0]), "helpers": installed,
            "modsTxt": str(mods_txt), "enabled": True}


# ---- desinstalacion (solo lo instalado por la tool) ----

def uninstall_paks(game: str, approved: bool = False) -> dict:
    """Elimina de ~mods los paks que la tool instalo (segun manifest)."""
    if not approved:
        raise PermissionError("uninstall_paks requiere aprobacion explicita del usuario")
    dest = gamepaths.paks_dir(game)
    m = load_manifest()
    removed = []
    for base in m.get("paks", []):
        for ext in (".pak", ".ucas", ".utoc"):
            f = dest / f"{base}{ext}"
            if f.exists():
                f.unlink()
                removed.append(f.name)
    m["paks"] = []
    _save_manifest(m)
    return {"removed": removed}


def uninstall_helper(game: str, approved: bool = False) -> dict:
    """Elimina las carpetas de helper que instalo la tool y las desactiva (=0)."""
    if not approved:
        raise PermissionError("uninstall_helper requiere aprobacion explicita del usuario")
    mods = gamepaths.ue4ss_mods_dir(game)
    m = load_manifest()
    names = m.get("helpers") or [HELPER_NAME]
    mods_txt = mods / "mods.txt"
    removed = []
    for name in names:
        dest = mods / name
        if dest.exists():
            shutil.rmtree(dest)
            removed.append(str(dest))
        if mods_txt.exists():
            mods_txt.write_text(set_mod_enabled(mods_txt, name, 0), encoding="utf-8")
    m["helpers"] = []
    m["helper"] = False
    _save_manifest(m)
    return {"removed": removed, "disabled": True}


if __name__ == "__main__":
    # smoke test de set_mod_enabled (no toca el juego)
    sample = ("JiggleControl : 1\n; Built-in keybinds, do not move up!\nKeybinds : 1\n")
    import tempfile
    p = Path(tempfile.mktemp(suffix=".txt"))
    p.write_text(sample, encoding="utf-8")
    print("--- insertar ---")
    print(set_mod_enabled(p, "StellarSoulsOutfitRestore", 1))
    p.write_text("StellarSoulsOutfitRestore: 0\nKeybinds : 1\n", encoding="utf-8")
    print("--- actualizar ---")
    print(set_mod_enabled(p, "StellarSoulsOutfitRestore", 1))
