"""buildjournal - deshacer un build que se corto a la mitad.

Cancelar mata el arbol de procesos (python + repak/retoc), asi que el propio
build no puede limpiar: no corre ningun finally. En vez de eso deja un diario en
disco antes de tocar nada, y la app llama despues a ``build_custom --rollback``,
que lee ese diario y:

  - restaura la instalacion previa (paks, helpers, mods.txt, manifest) desde el
    backup que se saco justo antes de instalar;
  - borra lo que el build dejo a medias en la carpeta de salida (``stage``,
    ``compile*`` y el zip parcial).

El diario apunta siempre al ultimo build arrancado: ``begin`` lo pisa. Un build
que termina bien llama a ``finish`` y no queda nada que deshacer.
"""
from __future__ import annotations

import json
import os
import shutil
import time
from pathlib import Path

# Carpetas intermedias que build() crea dentro de --out (ver build_custom.build).
ARTIFACT_DIRS = ("stage", "compile", "compile_mb", "compile_fr", "compile_hardcore")


def _state_dir() -> Path:
    base = os.environ.get("LOCALAPPDATA") or str(Path.home())
    d = Path(base) / "StellarSoulsBuilder"
    d.mkdir(parents=True, exist_ok=True)
    return d


def journal_path() -> Path:
    return _state_dir() / "pending_build.json"


def backup_root() -> Path:
    return _state_dir() / "rollback"


def load() -> dict | None:
    p = journal_path()
    if not p.exists():
        return None
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def _save(entry: dict) -> None:
    journal_path().write_text(json.dumps(entry, indent=2, ensure_ascii=False),
                              encoding="utf-8")


def begin(out_dir) -> dict:
    """Abre el diario del build que arranca ahora (pisa el anterior)."""
    # El backup del build previo ya no sirve: si aquel termino, no hay nada que
    # restaurar, y si murio, su rollback ya paso o quedo obsoleto.
    shutil.rmtree(backup_root(), ignore_errors=True)
    entry = {"out": str(Path(out_dir)), "started": time.time(), "backup": None}
    _save(entry)
    return entry


def record_backup(backup: dict) -> None:
    """Anota en el diario el backup de la instalacion previa a instalar."""
    entry = load()
    if entry is None:
        return
    entry["backup"] = backup
    _save(entry)


def finish() -> None:
    """Build terminado: no hay nada que deshacer."""
    journal_path().unlink(missing_ok=True)
    shutil.rmtree(backup_root(), ignore_errors=True)


def _clean_output(entry: dict) -> list[str]:
    """Borra las carpetas intermedias y el zip a medias de la carpeta de salida.

    Solo toca lo que este build pudo crear: las carpetas con nombre conocido y
    los zips ``StellarSouls-*.zip`` escritos despues de arrancar (un zip de un
    build anterior en la misma carpeta se conserva).
    """
    out = Path(entry.get("out", ""))
    removed = []
    if not out.is_dir():
        return removed
    for name in ARTIFACT_DIRS:
        d = out / name
        if d.is_dir():
            shutil.rmtree(d, ignore_errors=True)
            removed.append(str(d))
    started = float(entry.get("started", 0))
    for z in out.glob("StellarSouls-*.zip"):
        try:
            if z.is_file() and z.stat().st_mtime >= started:
                z.unlink()
                removed.append(str(z))
        except OSError:
            pass
    return removed


def rollback() -> dict:
    """Deshace el ultimo build arrancado. Idempotente: sin diario, no hace nada."""
    entry = load()
    if entry is None:
        return {"rolledBack": False}
    restored = {}
    if entry.get("backup"):
        import installer
        restored = installer.restore_install(entry["backup"])
    removed = _clean_output(entry)
    finish()
    return {"rolledBack": True, "restored": restored, "removed": removed}


if __name__ == "__main__":
    print(json.dumps(rollback(), indent=2, ensure_ascii=False))
