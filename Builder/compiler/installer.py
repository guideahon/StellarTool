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


PAK_SUFFIXES = (".pak", ".ucas", ".utoc")


def shadow_paks(game: str | None = None) -> list[str]:
    """Paks de Stellar Souls cargables que la tool NO instalo, o duplicados.

    ~mods se carga recursivamente, asi que una carpeta de build ahi adentro
    (``stage\\Paks``, ``compile_mb``) queda cargada y puede pisar al pak
    instalado sin que se vea por ningun lado. Devuelve rutas relativas a ~mods.

    Solo mira paks propios (prefijo ``StellarSouls``) y copias del instalado en
    subcarpetas: los mods de terceros viven en subcarpetas de forma legitima.
    """
    g = game or load_manifest().get("game") or gamepaths.detect_game()
    if not g:
        return []
    root = gamepaths.paks_dir(g)
    if not root.is_dir():
        return []
    installed = set(load_manifest().get("paks", []))
    out = []
    for f in root.rglob("*"):
        if f.suffix.lower() not in PAK_SUFFIXES or not f.is_file():
            continue
        in_subdir = f.parent != root
        mine = f.stem.startswith("StellarSouls")
        # Copia del instalado en subcarpeta, o pak propio que la tool no instalo.
        if (in_subdir and (mine or f.stem in installed)) or (mine and f.stem not in installed):
            rel = str(f.parent.relative_to(root) / f.stem)
            if rel not in out:
                out.append(rel)
    return sorted(out)


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
            "helper": bool(helpers_present), "helpers": helpers_present,
            "shadowPaks": shadow_paks(g) if g else []}


# ---- backup / restore para deshacer una instalacion cortada a la mitad ----

def _mods_txt_backup(d: Path) -> Path:
    return d / "mods.txt"


def backup_install(game: str, pak_bases, helper_names, dest_dir) -> dict:
    """Copia lo que la instalacion esta por pisar, para poder volver atras.

    ``pak_bases``/``helper_names`` son los que se van a instalar; se suma lo que
    ya figuraba en el manifest, porque un rollback tiene que dejar la
    instalacion anterior completa, no solo los archivos coincidentes.
    """
    dest_dir = Path(dest_dir)
    (dest_dir / "paks").mkdir(parents=True, exist_ok=True)
    (dest_dir / "helpers").mkdir(parents=True, exist_ok=True)
    m = load_manifest()

    bases = sorted(set(pak_bases) | set(m.get("paks", [])))
    mods = gamepaths.paks_dir(game)
    saved_paks = []
    for base in bases:
        for ext in PAK_SUFFIXES:
            f = mods / f"{base}{ext}"
            if f.is_file():
                shutil.copy2(f, dest_dir / "paks" / f.name)
                saved_paks.append(f.name)

    names = sorted(set(helper_names) | set(m.get("helpers") or []))
    ue4ss = gamepaths.ue4ss_mods_dir(game)
    saved_helpers, missing_helpers = [], []
    for name in names:
        src = ue4ss / name
        if src.is_dir():
            shutil.copytree(src, dest_dir / "helpers" / name, dirs_exist_ok=True)
            saved_helpers.append(name)
        else:
            missing_helpers.append(name)

    mods_txt = ue4ss / "mods.txt"
    had_mods_txt = mods_txt.is_file()
    if had_mods_txt:
        shutil.copy2(mods_txt, _mods_txt_backup(dest_dir))

    manifest = _manifest_path()
    had_manifest = manifest.is_file()
    if had_manifest:
        shutil.copy2(manifest, dest_dir / "installed.json")

    return {"game": game, "dir": str(dest_dir),
            "pakTargets": bases, "paks": saved_paks,
            "helperTargets": names, "helpers": saved_helpers,
            "helpersMissing": missing_helpers,
            "modsTxt": had_mods_txt, "manifest": had_manifest}


def restore_install(backup: dict) -> dict:
    """Vuelve la instalacion al estado que guardo ``backup_install``."""
    d = Path(backup.get("dir", ""))
    game = backup.get("game") or ""
    if not game or not d.is_dir():
        return {"restored": False}

    mods = gamepaths.paks_dir(game)
    # Primero fuera lo que haya ahora de esos paks (la instalacion cortada pudo
    # dejar solo el .pak sin su .utoc), despues volver a poner los del backup.
    for base in backup.get("pakTargets", []):
        for ext in PAK_SUFFIXES:
            f = mods / f"{base}{ext}"
            if f.is_file():
                f.unlink()
    for name in backup.get("paks", []):
        src = d / "paks" / name
        if src.is_file():
            mods.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, mods / name)

    ue4ss = gamepaths.ue4ss_mods_dir(game)
    for name in backup.get("helperTargets", []):
        dest = ue4ss / name
        if dest.is_dir():
            shutil.rmtree(dest, ignore_errors=True)
    for name in backup.get("helpers", []):
        src = d / "helpers" / name
        if src.is_dir():
            shutil.copytree(src, ue4ss / name, dirs_exist_ok=True)

    mods_txt = ue4ss / "mods.txt"
    if backup.get("modsTxt"):
        saved = _mods_txt_backup(d)
        if saved.is_file():
            shutil.copy2(saved, mods_txt)
    elif mods_txt.is_file():
        # No habia mods.txt antes: lo creo la instalacion que se corto.
        mods_txt.unlink()

    manifest = _manifest_path()
    if backup.get("manifest"):
        saved = d / "installed.json"
        if saved.is_file():
            shutil.copy2(saved, manifest)
    elif manifest.is_file():
        manifest.unlink()

    return {"restored": True, "paks": backup.get("paks", []),
            "helpers": backup.get("helpers", [])}


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


def helper_targets(helper_src) -> list[str]:
    """Nombres de mod UE4SS que instalaria install_helper desde helper_src."""
    return [p.name for p in _helper_sources(Path(helper_src))]


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


# ---- reconciliacion: sacar lo que la tool instalo y esta build ya no trae ----

def prune_paks(game: str, keep, approved: bool = False) -> dict:
    """Saca de ~mods los paks del manifest que esta build ya no produce.

    install_paks solo suma, asi que una build que cambia el nombre de su pak
    (Combat_P -> CombatNoOutfit_P) dejaba los dos cargados y el juego se
    quedaba con el que montara ultimo. Solo toca lo que figura en el manifest.
    """
    if not approved:
        raise PermissionError("prune_paks requiere aprobacion explicita del usuario")
    keep = set(keep)
    dest = gamepaths.paks_dir(game)
    m = load_manifest()
    removed = []
    for base in m.get("paks", []):
        if base in keep:
            continue
        for ext in PAK_SUFFIXES:
            f = dest / f"{base}{ext}"
            if f.is_file():
                f.unlink()
                removed.append(f.name)
    m["game"] = game
    m["paks"] = sorted(keep)
    _save_manifest(m)
    return {"removed": removed, "kept": sorted(keep)}


def prune_helpers(game: str, keep, approved: bool = False) -> dict:
    """Borra y pone en 0 los helpers del manifest que esta build ya no incluye.

    El helper CNS y las ALPHA vanilla son mods de UE4SS distintos y no deben
    correr a la vez; ademas, destildar el outfit tiene que apagar el restore que
    dejo la build anterior, no dejarlo en `: 1`.
    """
    if not approved:
        raise PermissionError("prune_helpers requiere aprobacion explicita del usuario")
    keep = set(keep)
    mods = gamepaths.ue4ss_mods_dir(game)
    mods_txt = mods / "mods.txt"
    m = load_manifest()
    names = m.get("helpers") or ([HELPER_NAME] if m.get("helper") else [])
    removed, disabled = [], []
    for name in names:
        if name in keep:
            continue
        dest = mods / name
        if dest.is_dir():
            shutil.rmtree(dest)
            removed.append(name)
        if mods_txt.is_file():
            mods_txt.write_text(set_mod_enabled(mods_txt, name, 0), encoding="utf-8")
            disabled.append(name)
    m["game"] = game
    m["helpers"] = sorted(keep)
    m["helper"] = bool(keep)
    _save_manifest(m)
    return {"removed": removed, "disabled": disabled, "kept": sorted(keep)}


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
