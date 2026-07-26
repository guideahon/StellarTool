"""history - historial de configuraciones compiladas.

Cada build queda registrado (respuestas + ruta del zip + fecha) en
%LOCALAPPDATA%\\StellarSoulsBuilder\\history\\<id>.json. Permite:
  - list(): ver builds anteriores (mas nuevo primero).
  - as_template(id): recuperar las respuestas para armar otro parecido.
  - reexport(id, out): volver a obtener el zip (copia si existe, o recompila).
"""
from __future__ import annotations

import json
import os
import time
from pathlib import Path


def _root() -> Path:
    base = os.environ.get("LOCALAPPDATA") or str(Path.home())
    d = Path(base) / "StellarSoulsBuilder" / "history"
    d.mkdir(parents=True, exist_ok=True)
    return d


def record(answers: dict, zip_path: str, label: str | None = None) -> str:
    hid = time.strftime("%Y%m%d-%H%M%S")
    rec = {
        "id": hid,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "label": label or _auto_label(answers),
        "answers": answers,
        "zip": str(zip_path),
    }
    (_root() / f"{hid}.json").write_text(json.dumps(rec, indent=2, ensure_ascii=False), encoding="utf-8")
    return hid


def _auto_label(a: dict) -> str:
    parts = [a.get("combatProfile", "?")]
    if a.get("outfitSkinSuit"):
        parts.append("outfit")
    if a.get("miniBoss", "off") not in ("off", False, None):
        parts.append("miniboss")
    return "+".join(parts)


def list_records() -> list[dict]:
    out = []
    for f in _root().glob("*.json"):
        try:
            out.append(json.loads(f.read_text(encoding="utf-8")))
        except (OSError, json.JSONDecodeError):
            continue
    return sorted(out, key=lambda r: r.get("id", ""), reverse=True)


def get(hid: str) -> dict | None:
    f = _root() / f"{hid}.json"
    return json.loads(f.read_text(encoding="utf-8")) if f.exists() else None


def as_template(hid: str) -> dict | None:
    """Respuestas de un build previo, para editar y recompilar."""
    rec = get(hid)
    return dict(rec["answers"]) if rec else None


def reexport(hid: str, out_dir: str) -> str:
    """Devuelve el zip de un build previo. Si el archivo original existe, lo copia;
    si no (se perdio), recompila desde las respuestas guardadas."""
    import shutil
    rec = get(hid)
    if not rec:
        raise KeyError(f"config {hid} no existe en el historial")
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    src = Path(rec.get("zip", ""))
    if src.exists():
        dst = out_dir / src.name
        shutil.copy2(src, dst)
        return str(dst)
    # perdido: recompilar
    import build_custom
    return str(build_custom.build(rec["answers"], out_dir))


if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1 and sys.argv[1] == "list":
        for r in list_records():
            print(f"{r['id']}  {r['label']:20}  {r.get('zip','')}")
    else:
        print("history root:", _root())
