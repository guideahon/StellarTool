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
import threading
from pathlib import Path

# UAssetGUI NO tolera instancias concurrentes: dos procesos a la vez terminan
# con exit 0 sin escribir nada (usa recursos globales, entre ellos el
# portapapeles para reportar errores). Correr las tablas en paralelo parecia
# ahorrar ~15s y fallaba de forma intermitente con el mismo mensaje que ve el
# usuario cuando falta un FName. El lock deja los procesos en fila aunque el
# llamador use threads.
_UAG_LOCK = threading.Lock()

_DEFAULT_TOOLS = Path(r"C:\Users\cristian\Documents\Stellar Tool\tools")
_VENDOR_TOOLS = Path(__file__).resolve().parent.parent / "vendor" / "tools"


def tools_dir() -> Path:
    env = os.environ.get("STELLAR_TOOLS")
    if env:
        return Path(env)
    if _VENDOR_TOOLS.exists():  # distribucion portable
        return _VENDOR_TOOLS
    return _DEFAULT_TOOLS


def _run(args: list[str], timeout: int = 900, extra_path: Path | None = None,
         cwd: Path | None = None) -> subprocess.CompletedProcess:
    env = None
    if extra_path is not None:
        env = dict(os.environ)
        env["PATH"] = str(extra_path) + os.pathsep + env.get("PATH", "")
    # encoding explicito: con text=True Python decodifica con el locale (cp936 en
    # Windows chino) y la salida UTF-8 de retoc/UAssetGUI revienta el hilo lector
    # dejando stdout=None -> el error real quedaba tapado por un TypeError.
    return subprocess.run(args, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=timeout, env=env,
                          cwd=str(cwd) if cwd else None)


def out_err(cp: subprocess.CompletedProcess, limit: int = 500) -> str:
    """Cola de stdout+stderr, tolerante a None (proceso sin salida capturada)."""
    return ((cp.stdout or "") + (cp.stderr or ""))[-limit:]


def fromjson(json_path: Path, out_uasset: Path) -> Path:
    """JSON UAssetAPI -> .uasset (+.uexp). Falla si no se escribe el archivo."""
    exe = tools_dir() / "UAssetGUI.exe"
    out_uasset = Path(out_uasset)
    out_uasset.parent.mkdir(parents=True, exist_ok=True)
    with _UAG_LOCK:
        cp = _run([str(exe), "fromjson", str(json_path), str(out_uasset), "StellarBlade"])
    if not out_uasset.exists() or out_uasset.stat().st_size == 0:
        raise RuntimeError(
            f"fromjson no escribio {out_uasset.name} (probable FName faltante en NameMap). "
            f"salida={out_err(cp, 800)}"
        )
    return out_uasset


def tojson(uasset: Path, out_json: Path, timeout: int = 900) -> Path:
    """.uasset -> JSON UAssetAPI. Falla si no se escribe el archivo."""
    exe = tools_dir() / "UAssetGUI.exe"
    out_json = Path(out_json)
    out_json.parent.mkdir(parents=True, exist_ok=True)
    with _UAG_LOCK:
        cp = _run([str(exe), "tojson", str(uasset), str(out_json), "VER_UE4_26",
                   str(tools_dir() / "StellarBlade.usmap")], timeout)
    if not out_json.exists():
        raise RuntimeError(
            f"tojson no genero el JSON de {Path(uasset).name}. salida={out_err(cp, 800)}")
    return out_json


def edit_uasset(uasset: Path, mutators, repair: bool = True) -> dict:
    """Aplica varias ediciones a un .uasset en UN solo tojson/fromjson.

    Cada roundtrip de una tabla grande (EffectTable) cuesta ~25s, asi que los
    pases que tocan la misma tabla se agrupan aca. ``mutators`` es una lista de
    callables doc -> reporte, aplicados en orden.

    repair=False para el camino fiel, donde la salida se compara byte a byte
    contra el pak publico y agregar entradas al NameMap la correria.
    """
    import json
    uasset = Path(uasset)
    if not uasset.exists() or not mutators:
        return {}
    tmp = uasset.with_name(f"_{uasset.stem}_edit.json")
    tojson(uasset, tmp)
    doc = json.loads(tmp.read_text(encoding="utf-8"))
    report = {}
    for mutate in mutators:
        report.update(mutate(doc) or {})
    if repair:
        import table_compiler   # local: table_compiler importa este modulo
        table_compiler.repair_namemap(doc)
    tmp.write_text(json.dumps(doc), encoding="utf-8")
    # fromjson pisa un .uasset que ya existe: chequear existencia no alcanza
    # para detectar el fallo silencioso, hay que ver que se reescribio.
    before = uasset.stat().st_mtime_ns
    with _UAG_LOCK:
        cp = _run([str(tools_dir() / "UAssetGUI.exe"), "fromjson", str(tmp),
                   str(uasset), "StellarBlade"])
    # UAssetGUI puede fallar dejando el uasset sin tocar o directamente
    # borrandolo: los dos casos son el mismo problema y merecen el mismo
    # mensaje, no un FileNotFoundError pelado desde el stat.
    after = uasset.stat().st_mtime_ns if uasset.exists() else None
    if after is None or after == before:
        raise RuntimeError(
            f"fromjson no reescribio {uasset.name} (probable FName faltante en "
            f"NameMap). salida={out_err(cp, 800)}")
    tmp.unlink(missing_ok=True)
    return report


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


def ensure_oodle() -> Path | None:
    """Garantiza que retoc encuentre oo2core. retoc busca la DLL JUNTO A SU
    PROPIO EXE (no usa PATH) y si no está intenta DESCARGARLA de GitHub, lo que
    cuelga o revienta sin red o con GitHub bloqueado (China). Se copia la DLL
    del juego del usuario junto a retoc.exe en runtime; no se redistribuye
    (package.bat la excluye del zip). Devuelve el dir con la DLL o None."""
    d = oodle_dir()
    if d is None:
        return None
    target = tools_dir() / _OODLE
    if not target.exists() and Path(d) != tools_dir():
        try:
            shutil.copy2(Path(d) / _OODLE, target)
        except OSError:
            pass  # tools/ de solo lectura: PATH + cwd como mejor esfuerzo
    return d


def _retoc(args: list[str], timeout: int = 900) -> subprocess.CompletedProcess:
    """Corre retoc asegurando oo2core junto al exe (ver ensure_oodle)."""
    oodle = ensure_oodle()
    return _run([str(tools_dir() / "retoc.exe"), *args], timeout,
                extra_path=oodle, cwd=oodle)


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
        raise RuntimeError(f"to-zen fallo: {out_err(cp, 800)}")
    return out_utoc


def verify(utoc: Path) -> bool:
    cp = _retoc(["verify", str(utoc)])
    out = out_err(cp, 100000).lower()
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
