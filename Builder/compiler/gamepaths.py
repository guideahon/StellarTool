"""gamepaths - autodeteccion de la instalacion de Stellar Blade.

Busca en ubicaciones comunes de Steam/Epic + libraryfolders.vdf + registro.
Devuelve la carpeta raiz del juego (la que contiene SB\\Content y SB\\Binaries).
La ruta elegida en la UI llega por env STELLARBLADE_DIR (la setea la app antes
de lanzar el Builder), asi que un install fuera de Steam igual funciona.
"""
from __future__ import annotations

import os
import re
import string
from pathlib import Path

_SUFFIX = Path("steamapps") / "common" / "StellarBlade"
_MARKER = Path("SB") / "Content" / "Paks"  # confirma que es la instalacion
# Layouts no-Steam relativos a la raiz de cada unidad.
_OTHER_SUFFIXES = [
    Path("Program Files") / "Epic Games" / "StellarBlade",
    Path("Epic Games") / "StellarBlade",
    Path("Games") / "StellarBlade",
    Path("StellarBlade"),
]

ENV_VAR = "STELLARBLADE_DIR"


def is_game(root) -> bool:
    """True si ``root`` es la raiz de la instalacion (tiene SB/Content/Paks)."""
    try:
        return bool(root) and (Path(root) / _MARKER).is_dir()
    except OSError:
        return False


# Compat: nombre viejo, privado.
_is_game = is_game


def _drives():
    """Unidades existentes (se saltean A:/B: para no despertar disketeras)."""
    out = []
    for letter in string.ascii_uppercase[2:]:
        try:
            if Path(f"{letter}:/").exists():
                out.append(letter)
        except OSError:
            pass
    return out


def _steam_roots():
    """Instalaciones de Steam: registro + rutas por defecto."""
    roots = [Path(r"C:\Program Files (x86)\Steam"), Path(r"C:\Program Files\Steam")]
    try:
        import winreg
        for hive, key, value in (
            (winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
            (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath"),
        ):
            try:
                with winreg.OpenKey(hive, key) as k:
                    path = winreg.QueryValueEx(k, value)[0]
                if path:
                    roots.append(Path(path.replace("/", "\\")))
            except OSError:
                pass
    except ImportError:  # no-Windows
        pass
    return roots


def _steam_libraries():
    """Rutas de bibliotecas Steam desde libraryfolders.vdf + defaults."""
    candidates = list(_steam_roots())
    for drive in _drives():
        candidates.append(Path(f"{drive}:/SteamLibrary"))
        candidates.append(Path(f"{drive}:/Steam"))
        candidates.append(Path(f"{drive}:/Games/SteamLibrary"))
    # libraryfolders.vdf lista todas las bibliotecas configuradas
    for steam in _steam_roots():
        vdf = steam / "steamapps" / "libraryfolders.vdf"
        if vdf.exists():
            try:
                for m in re.finditer(r'"path"\s*"([^"]+)"',
                                     vdf.read_text(encoding="utf-8", errors="ignore")):
                    candidates.append(Path(m.group(1).replace("\\\\", "\\")))
            except OSError:
                pass
    seen, out = set(), []
    for c in candidates:
        if c not in seen:
            seen.add(c); out.append(c)
    return out


def detect_game(explicit=None) -> str | None:
    """Devuelve la ruta del juego, o None si no se encuentra.

    Orden: ``explicit`` (la que eligio el usuario en la UI) -> env
    STELLARBLADE_DIR -> bibliotecas Steam -> layouts no-Steam por unidad.
    """
    if is_game(explicit):
        return str(Path(explicit))
    env = os.environ.get(ENV_VAR)
    if is_game(env):
        return str(Path(env))
    for lib in _steam_libraries():
        root = lib / _SUFFIX
        if is_game(root):
            return str(root)
    for drive in _drives():
        for suffix in _OTHER_SUFFIXES:
            root = Path(f"{drive}:/") / suffix
            if is_game(root):
                return str(root)
    return None


def remember_game(path) -> str | None:
    """Publica la ruta en STELLARBLADE_DIR para el resto del proceso.

    Los pasos que extraen baselines vanilla (table_compiler) resuelven el juego
    por su cuenta; sin esto, la ruta elegida en la UI no les llegaba.
    """
    game = detect_game(path)
    if game:
        os.environ[ENV_VAR] = game
    return game


def paks_dir(game: str) -> Path:
    return Path(game) / "SB" / "Content" / "Paks" / "~mods"


def ue4ss_mods_dir(game: str) -> Path:
    return Path(game) / "SB" / "Binaries" / "Win64" / "ue4ss" / "Mods"


def _norm_parts(path) -> tuple:
    """Partes de la ruta normalizadas para comparar (Windows: case-insensitive)."""
    try:
        resolved = Path(path).expanduser().resolve()
    except (OSError, ValueError):
        resolved = Path(path)
    return tuple(os.path.normcase(p) for p in resolved.parts)


def is_inside_mods(path, game: str | None = None) -> bool:
    """True si ``path`` es ``~mods`` o cuelga de ahi.

    El juego carga ``~mods`` **recursivamente**: una carpeta de build ahi adentro
    deja paks cargables que la UI no muestra y que pisan al mod instalado. Se
    compara por partes de ruta, asi que un hermano con prefijo comun (``~modsBk``)
    no cuenta.
    """
    game = game or detect_game()
    if not game or not path:
        return False
    mods = _norm_parts(paks_dir(game))
    target = _norm_parts(path)
    return target[:len(mods)] == mods


if __name__ == "__main__":
    g = detect_game()
    print(g or "NO ENCONTRADO")
    if g:
        print("~mods:", paks_dir(g), "| existe:", paks_dir(g).exists())
        print("ue4ss/Mods:", ue4ss_mods_dir(g), "| existe:", ue4ss_mods_dir(g).exists())
