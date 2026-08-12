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
import tempfile
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


def is_wine() -> bool:
    """True si este Python corre bajo Wine/Proton (Linux, Steam Deck).

    Deteccion canonica: ntdll exporta wine_get_version solo bajo Wine.
    """
    try:
        import ctypes
        ntdll = ctypes.WinDLL("ntdll")
        return hasattr(ntdll, "wine_get_version")
    except Exception:
        return False


def clipboard_text(limit: int = 800) -> str:
    """Texto del portapapeles, o "" si no se puede leer. Solo lectura.

    UAssetGUI reporta sus errores por MessageBox y ademas los DEJA EN EL
    PORTAPAPELES. Es la unica via: es una app WinForms, no escribe una linea en
    la consola ni cuando falla (por eso ``salida=`` siempre venia vacia).
    """
    try:
        import ctypes
        u32, k32 = ctypes.WinDLL("user32"), ctypes.WinDLL("kernel32")
        CF_UNICODETEXT = 13
        if not u32.IsClipboardFormatAvailable(CF_UNICODETEXT):
            return ""
        if not u32.OpenClipboard(None):
            return ""
        try:
            h = u32.GetClipboardData(CF_UNICODETEXT)
            if not h:
                return ""
            k32.GlobalLock.restype = ctypes.c_void_p
            p = k32.GlobalLock(ctypes.c_void_p(h))
            if not p:
                return ""
            try:
                return ctypes.c_wchar_p(p).value[:limit] or ""
            finally:
                k32.GlobalUnlock(ctypes.c_void_p(h))
        finally:
            u32.CloseClipboard()
    except Exception:
        return ""


def _uag_failure(what: str, cp: subprocess.CompletedProcess) -> str:
    """Mensaje de fallo de UAssetGUI con la causa mas probable segun el entorno.

    Bajo Wine UAssetGUI (WinForms/.NET) puede morir sin escribir NADA y sin
    salida: el mensaje generico de "FName faltante" mandaba al usuario a buscar
    un bug que no existe. Se distingue por rc y por la ausencia de salida.
    """
    tail = out_err(cp, 800)
    msg = f"{what} (UAssetGUI rc={cp.returncode})."
    if tail:
        msg += f" salida={tail}"
    clip = clipboard_text()
    if clip:
        msg += f"\nUAssetGUI dejo esto en el portapapeles:\n{clip}"
    msg += (
        "\nUAssetGUI termino sin escribir el archivo. Causas posibles:"
        "\n - un FName nuevo falta en el NameMap;"
        "\n - la ruta de salida es muy larga o tiene caracteres no ASCII"
        " (proba armar el mod en una carpeta corta tipo C:\\StellarTool);"
        "\n - el antivirus bloqueo UAssetGUI.exe.")
    if is_wine():
        msg += "\n" + WINE_HINT
    return msg


WINE_HINT = (
    "Estas bajo Wine/Proton: UAssetGUI es una app .NET/WinForms y necesita el "
    "runtime de .NET Y las fuentes de Windows en el prefijo (sin la fuente "
    "falla al dibujar y muere sin hacer nada). En el prefijo de Stellar Blade "
    "(appid 3489700):\n"
    "  protontricks 3489700 micross\n"
    "  protontricks 3489700 dotnetdesktop8\n"
    "  protontricks 3489700 dotnet8\n"
    "Si corres Stellar Tool en otro prefijo, usa el appid de ese prefijo. Las "
    "pestanas de merge no necesitan nada de esto.")


# Tabla vendorizada chica: alcanza para probar el toolchain entero (runtime
# .NET, fuentes, usmap) en menos de un segundo.
_PROBE_TABLE = "ItemEquipableTable"


def selftest_uassetgui() -> str | None:
    """Chequea que UAssetGUI FUNCIONE. None si esta OK, si no el motivo.

    Un build extrae baselines y compila tablas (minutos) antes de necesitar el
    exe: si nunca iba a correr, hay que decirlo ya. La prueba es un tojson real
    sobre una tabla chica que viene con el Builder. No sirve invocarlo con
    argumentos invalidos: con menos de los que espera abre la GUI (le pasamos
    "fromjson" y trato de ABRIR un archivo llamado fromjson) y no reporta nada
    por consola ni cuando anda bien.
    """
    exe = tools_dir() / "UAssetGUI.exe"
    if not exe.exists():
        return f"Falta {exe}. Reinstala Stellar Tool (carpeta tools\\ incompleta)."
    src = (Path(__file__).resolve().parent.parent / "vendor" / "stagings" /
           "firstRun" / "SB" / "Content" / "Local" / "Data" /
           f"{_PROBE_TABLE}.uasset")
    if not src.exists():
        return None   # sin la tabla de prueba no hay chequeo posible
    tmp = Path(tempfile.mkdtemp(prefix="st_uag_probe_"))
    try:
        for ext in (".uasset", ".uexp"):
            if src.with_suffix(ext).exists():
                shutil.copy2(src.with_suffix(ext), tmp / f"{_PROBE_TABLE}{ext}")
        out = tmp / "probe.json"
        try:
            cp = _run([str(exe), "tojson", str(tmp / f"{_PROBE_TABLE}.uasset"),
                       str(out), "VER_UE4_26",
                       str(tools_dir() / "StellarBlade.usmap")], timeout=180)
        except subprocess.TimeoutExpired:
            return ("UAssetGUI.exe se colgo en la prueba de arranque (probable "
                    "ventana de error esperando un click)."
                    + ("\n" + WINE_HINT if is_wine() else ""))
        except OSError as e:
            return f"No se pudo ejecutar UAssetGUI.exe: {e}"
        if out.exists() and out.stat().st_size > 0:
            return None
        return _uag_failure(
            "UAssetGUI.exe no puede procesar tablas (prueba de arranque)", cp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def _uag_fromjson(json_path: Path, out_uasset: Path) -> subprocess.CompletedProcess:
    with _UAG_LOCK:
        return _run([str(tools_dir() / "UAssetGUI.exe"), "fromjson",
                     str(json_path), str(out_uasset), "StellarBlade"])


def _short_workdir() -> Path:
    """Dir de trabajo corto y ASCII para UAssetGUI.

    UAssetGUI (WinForms/.NET Framework) muere sin escribir nada ni reportar
    error cuando la ruta pasa los 260 chars o trae caracteres fuera del
    codepage del sistema (tipico en Windows chino/coreano: el mod se arma en
    "桌面\\...\\"). Es indistinguible del fallo por FName faltante.
    """
    import tempfile
    # Un directorio por intento evita que una corrida anterior (o dos builds
    # de procesos distintos) deje un .uasset que parezca un resultado válido.
    return Path(tempfile.mkdtemp(prefix="st_uag_"))


def _is_awkward(p: Path) -> bool:
    s = str(p)
    return len(s) > 200 or not s.isascii()


def fromjson(json_path: Path, out_uasset: Path) -> Path:
    """JSON UAssetAPI -> .uasset (+.uexp). Falla si no se escribe el archivo."""
    out_uasset = Path(out_uasset)
    json_path = Path(json_path)
    out_uasset.parent.mkdir(parents=True, exist_ok=True)
    # Un reintento: UAssetGUI falla de forma intermitente (recursos globales,
    # portapapeles) y rehacer la tabla cuesta segundos frente a perder el build.
    for _ in range(2):
        cp = _uag_fromjson(json_path, out_uasset)
        if out_uasset.exists() and out_uasset.stat().st_size > 0:
            return out_uasset
    # Ultimo intento SIEMPRE en una ruta corta/ASCII. La detección por longitud
    # no alcanza: Windows puede rechazar rutas aparentemente cortas por el
    # directorio padre, el codepage o una combinación de caracteres. Este
    # fallback también separa un problema de ruta de uno real del JSON.
    tmp = _short_workdir()
    tmp_json = tmp / "in.json"
    tmp_out = tmp / out_uasset.name
    try:
        shutil.copy2(json_path, tmp_json)
        cp2 = _uag_fromjson(tmp_json, tmp_out)
        if tmp_out.exists() and tmp_out.stat().st_size > 0:
            shutil.copy2(tmp_out, out_uasset)
            # UAssetGUI puede emitir sidecars además de .uexp; copiar los
            # archivos generados con el mismo stem conserva el round-trip.
            for sidecar in tmp.glob(f"{tmp_out.stem}.*"):
                if sidecar != tmp_out:
                    shutil.copy2(sidecar, out_uasset.with_suffix(sidecar.suffix))
            return out_uasset
        cp = cp2
    except OSError:
        pass
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    raise RuntimeError(_uag_failure(f"fromjson no escribio {out_uasset.name}", cp))


def tojson(uasset: Path, out_json: Path, timeout: int = 900) -> Path:
    """.uasset -> JSON UAssetAPI. Falla si no se escribe el archivo."""
    exe = tools_dir() / "UAssetGUI.exe"
    out_json = Path(out_json)
    out_json.parent.mkdir(parents=True, exist_ok=True)
    with _UAG_LOCK:
        cp = _run([str(exe), "tojson", str(uasset), str(out_json), "VER_UE4_26",
                   str(tools_dir() / "StellarBlade.usmap")], timeout)
    if not out_json.exists():
        raise RuntimeError(_uag_failure(
            f"tojson no genero el JSON de {Path(uasset).name}", cp))
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
        raise RuntimeError(_uag_failure(f"fromjson no reescribio {uasset.name}", cp))
    tmp.unlink(missing_ok=True)
    return report


_OODLE = "oo2core_9_win64.dll"


_OODLE_CACHE: list = []   # [] = sin resolver; [valor] = resuelto (puede ser None)


def oodle_dir() -> Path | None:
    """Directorio que contiene oo2core (Oodle). NO se redistribuye (propietario):
    se usa DIRECTO de la instalacion del juego del usuario (sin copiar al tool).
    Orden: junto a retoc (dev), luego el juego detectado. None si no se encuentra.
    Override: env STELLAR_OODLE_DIR. El resultado se cachea: el fallback recorre
    toda la carpeta del juego y se consulta en cada tabla."""
    if _OODLE_CACHE:
        return _OODLE_CACHE[0]
    found = _oodle_dir_uncached()
    _OODLE_CACHE.append(found)
    return found


def _oodle_in(d) -> Path | None:
    """La DLL dentro de ``d``, ignorando mayusculas. None si no esta."""
    d = Path(d)
    if (d / _OODLE).exists():
        return d
    try:
        target = _OODLE.lower()
        for f in d.iterdir():
            if f.is_file() and f.name.lower() == target:
                return d
    except OSError:
        pass
    return None


def _oodle_dir_uncached() -> Path | None:
    # La app pasa STELLAR_OODLE_DIR ya resuelto y puede apuntar al archivo.
    env = os.environ.get("STELLAR_OODLE_DIR")
    if env:
        p = Path(env)
        if p.is_file():
            return p.parent
        hit = _oodle_in(p)
        if hit:
            return hit
    if _oodle_in(tools_dir()):   # copia local dev (no en el zip)
        return tools_dir()
    try:
        import gamepaths
        game = gamepaths.detect_game()
    except Exception:
        game = None
    if game:
        g = Path(game)
        for c in (g / "SB" / "Binaries" / "Win64",
                  g / "SB" / "Binaries" / "Win64" / "ThirdParty" / "Oodle",
                  g / "Engine" / "Binaries" / "ThirdParty" / "Oodle" / "Win64",
                  g / "Engine" / "Binaries" / "Win64",
                  g / "CNSRepacker" / "tools" / "retoc",
                  g):
            if _oodle_in(c):
                return c
        hit = _find_oodle(g)  # fallback: cualquier copia, sin importar mayusculas
        if hit:
            return hit.parent
    return None


def _find_oodle(root: Path) -> Path | None:
    """Busca oo2core en ``root`` ignorando mayusculas.

    En Linux (Proton/Steam Deck) el filesystem distingue mayusculas y el DLL
    puede estar como ``oo2core_9_win64.DLL``: ``rglob`` compara texto plano y no
    lo encontraba, con lo que el build moria diciendo que el juego no lo tiene.
    """
    target = _OODLE.lower()
    try:
        for dirpath, _dirnames, filenames in os.walk(root):
            for f in filenames:
                if f.lower() == target:
                    return Path(dirpath) / f
    except OSError:
        pass
    return None


def missing_oodle_msg() -> str:
    """Mensaje de "falta Oodle" que distingue las dos causas.

    Antes decia siempre "no se pudo detectar Stellar Blade", incluso con el
    juego detectado y el DLL ausente: mandaba a reinstalar el juego a alguien
    cuyo problema era otro.
    """
    try:
        import gamepaths
        game = gamepaths.detect_game()
    except Exception:
        game = None
    where = (f"Se busco en la instalacion detectada ({game}) y no esta ahi."
             if game else
             "Ademas no se pudo detectar la instalacion de Stellar Blade: "
             "elegi la carpeta del juego (la que contiene SB\\Content\\Paks).")
    return (f"No se encontro {_OODLE} (Oodle). Es propietaria: no se "
            f"redistribuye, se usa directo del juego. {where}\n"
            f"Salidas: copia {_OODLE} a la carpeta tools\\ de Stellar Tool, o "
            "define STELLAR_OODLE_DIR con la carpeta que la contiene.")


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
        raise RuntimeError(missing_oodle_msg())
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
