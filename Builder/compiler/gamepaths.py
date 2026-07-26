"""gamepaths - autodeteccion de la instalacion de Stellar Blade.

Busca en ubicaciones comunes de Steam + libraryfolders.vdf + registro. Devuelve
la carpeta raiz del juego (la que contiene SB\\Content y SB\\Binaries).
"""
from __future__ import annotations

import os
import re
from pathlib import Path

_SUFFIX = Path("steamapps") / "common" / "StellarBlade"
_MARKER = Path("SB") / "Content" / "Paks"  # confirma que es la instalacion


def _is_game(root: Path) -> bool:
    return (root / _MARKER).is_dir()


def _steam_libraries():
    """Rutas de bibliotecas Steam desde libraryfolders.vdf + defaults."""
    libs = []
    candidates = [
        Path(r"C:\Program Files (x86)\Steam"),
        Path(r"C:\Program Files\Steam"),
    ]
    for drive in "CDEFGH":
        candidates.append(Path(f"{drive}:/SteamLibrary"))
        candidates.append(Path(f"{drive}:/Steam"))
    # libraryfolders.vdf lista todas las bibliotecas configuradas
    for steam in (Path(r"C:\Program Files (x86)\Steam"), Path(r"C:\Program Files\Steam")):
        vdf = steam / "steamapps" / "libraryfolders.vdf"
        if vdf.exists():
            try:
                for m in re.finditer(r'"path"\s*"([^"]+)"', vdf.read_text(encoding="utf-8", errors="ignore")):
                    candidates.append(Path(m.group(1).replace("\\\\", "\\")))
            except OSError:
                pass
    seen, out = set(), []
    for c in candidates:
        if c not in seen:
            seen.add(c); out.append(c)
    return out


def detect_game() -> str | None:
    """Devuelve la ruta del juego, o None si no se encuentra. Override: env
    STELLARBLADE_DIR."""
    env = os.environ.get("STELLARBLADE_DIR")
    if env and _is_game(Path(env)):
        return str(Path(env))
    for lib in _steam_libraries():
        root = lib / _SUFFIX
        if _is_game(root):
            return str(root)
    return None


def paks_dir(game: str) -> Path:
    return Path(game) / "SB" / "Content" / "Paks" / "~mods"


def ue4ss_mods_dir(game: str) -> Path:
    return Path(game) / "SB" / "Binaries" / "Win64" / "ue4ss" / "Mods"


if __name__ == "__main__":
    g = detect_game()
    print(g or "NO ENCONTRADO")
    if g:
        print("~mods:", paks_dir(g), "| existe:", paks_dir(g).exists())
        print("ue4ss/Mods:", ue4ss_mods_dir(g), "| existe:", ue4ss_mods_dir(g).exists())
