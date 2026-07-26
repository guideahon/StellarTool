"""toolchain — wrappers de UAssetGUI / retoc para compilar tablas a pak Zen.

Recipe (de Development/MODDING_GUIDE.md):
  1. UAssetGUI.exe fromjson <in.json> <out.uasset> StellarBlade  -> .uasset + .uexp
  2. stage bajo  package/SB/Content/Local/Data/<Table>.uasset(+.uexp)
  3. retoc.exe to-zen package "<Name>_P.utoc" --version UE4_26   -> .pak/.ucas/.utoc
  4. retoc.exe verify "<Name>_P.utoc"                             -> "verified"

Rutas de binarios: por defecto Stellar Tool/tools/. Override con env STELLAR_TOOLS.
GOTCHA critico (documentado): si un FName nuevo falta en NameMap, fromjson escribe
NADA (exit 0, sin archivo). Por eso verificamos que el .uasset se haya creado.
"""
from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

_DEFAULT_TOOLS = Path(r"C:\Users\cristian\Documents\Stellar Tool\tools")
_VENDOR_TOOLS = Path(__file__).resolve().parent.parent / "vendor" / "tools"


def tools_dir() -> Path:
    env = os.environ.get("STELLAR_TOOLS")
    if env:
        return Path(env)
    if _VENDOR_TOOLS.exists():  # distribucion portable
        return _VENDOR_TOOLS
    return _DEFAULT_TOOLS


def _run(args: list[str], timeout: int = 900, extra_path: Path | None = None) -> subprocess.CompletedProcess:
    env = None
    if extra_path is not None:
        env = dict(os.environ)
        env["PATH"] = str(extra_path) + os.pathsep + env.get("PATH", "")
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout, env=env)


def fromjson(json_path: Path, out_uasset: Path) -> Path:
    """JSON UAssetAPI -> .uasset (+.uexp). Falla si no se escribe el archivo."""
    exe = tools_dir() / "UAssetGUI.exe"
    out_uasset = Path(out_uasset)
    out_uasset.parent.mkdir(parents=True, exist_ok=True)
    cp = _run([str(exe), "fromjson", str(json_path), str(out_uasset), "StellarBlade"])
    if not out_uasset.exists() or out_uasset.stat().st_size == 0:
        raise RuntimeError(
            f"fromjson no escribio {out_uasset.name} (probable FName faltante en NameMap). "
            f"stdout={cp.stdout[-400:]} stderr={cp.stderr[-400:]}"
        )
    return out_uasset


_OODLE = "oo2core_9_win64.dll"


def oodle_dir() -> Path | None:
    """Directorio que contiene oo2core (Oodle). NO se redistribuye (propietario):
    se usa DIRECTO de la instalacion del juego del usuario (sin copiar al tool).
    Orden: junto a retoc (dev), luego el juego detectado. None si no se encuentra.
    Override: env STELLAR_OODLE_DIR."""
    env = os.environ.get("STELLAR_OODLE_DIR")
    if env and (Path(env) / _OODLE).exists():
        return Path(env)
    if (tools_dir() / _OODLE).exists():   # copia local dev (no en el zip)
        return tools_dir()
    try:
        import gamepaths
        game = gamepaths.detect_game()
    except Exception:
        game = None
    if game:
        g = Path(game)
        for c in (g / "SB" / "Binaries" / "Win64",
                  g / "CNSRepacker" / "tools" / "retoc"):
            if (c / _OODLE).exists():
                return c
        hit = next(iter(g.rglob(_OODLE)), None)  # fallback: cualquier copia
        if hit:
            return hit.parent
    return None


def _retoc(args: list[str], timeout: int = 900) -> subprocess.CompletedProcess:
    """Corre retoc con oo2core del juego en el PATH (sin copiarlo al tool)."""
    return _run([str(tools_dir() / "retoc.exe"), *args], timeout, extra_path=oodle_dir())


def to_zen(package_dir: Path, out_utoc: Path, version: str = "UE4_26") -> Path:
    """Empaqueta package_dir (con SB/Content/Local/Data/*) a pak Zen."""
    if oodle_dir() is None:
        raise RuntimeError(
            "No se encontro oo2core_9_win64.dll (Oodle). Se usa directo del juego "
            "y no se pudo detectar Stellar Blade. Instala/abri el juego una vez, o "
            "define STELLAR_OODLE_DIR con la carpeta del DLL.")
    out_utoc = Path(out_utoc)
    out_utoc.parent.mkdir(parents=True, exist_ok=True)
    cp = _retoc(["to-zen", str(package_dir), str(out_utoc), "--version", version])
    produced = out_utoc.with_suffix(".pak")
    if cp.returncode != 0 or not produced.exists():
        raise RuntimeError(f"to-zen fallo: {cp.stdout[-400:]} {cp.stderr[-400:]}")
    return out_utoc


def verify(utoc: Path) -> bool:
    cp = _retoc(["verify", str(utoc)])
    out = (cp.stdout + cp.stderr).lower()
    return "verified" in out


def stage_and_pack(uassets: list[Path], pak_name: str, work_dir: Path,
                   verify_result: bool = True) -> dict:
    """Estagea uassets (+ sus .uexp) bajo package/SB/Content/Local/Data y packea.

    Devuelve {'pak','ucas','utoc','verified'}.
    """
    work_dir = Path(work_dir)
    data_dir = work_dir / "package" / "SB" / "Content" / "Local" / "Data"
    data_dir.mkdir(parents=True, exist_ok=True)
    for ua in uassets:
        ua = Path(ua)
        shutil.copy2(ua, data_dir / ua.name)
        uexp = ua.with_suffix(".uexp")
        if uexp.exists():
            shutil.copy2(uexp, data_dir / uexp.name)
    utoc = work_dir / f"{pak_name}_P.utoc"
    to_zen(work_dir / "package", utoc)
    ok = verify(utoc) if verify_result else None
    return {
        "pak": utoc.with_suffix(".pak"),
        "ucas": utoc.with_suffix(".ucas"),
        "utoc": utoc,
        "verified": ok,
    }
