"""questionnaire — cuestionario interactivo del Builder (CLI, 10 idiomas).

Lee features/manifest.json + i18n/questionnaire.json, hace las preguntas en el
idioma elegido, respeta showIf, y llama build_custom para compilar el mod.

Uso:
    python questionnaire.py [--lang es] [--out <dir>]

Es el front-end de referencia del flujo "cuestionario -> compila". Una UI QML
(Stellar Tool) puede reemplazar la capa de preguntas llamando a build_custom
igual que aca.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_custom

BUILDER = Path(__file__).resolve().parent.parent
MANIFEST = json.loads((BUILDER / "features" / "manifest.json").read_text(encoding="utf-8"))
I18N = json.loads((BUILDER / "i18n" / "questionnaire.json").read_text(encoding="utf-8"))["strings"]
LANGS = build_custom.SUPPORTED_LANGS


def t(key, lang):
    entry = I18N.get(key, {})
    return entry.get(lang) or entry.get("en") or key


def _show(question, answers):
    cond = question.get("showIf")
    if not cond:
        return True
    for field, allowed in cond.items():
        if answers.get(field) not in allowed:
            return False
    return True


def ask(question, lang, answers):
    qid = question["id"]
    label = t(question["i18nKey"], lang)
    qtype = question["type"]
    if qtype in ("single", "bool"):
        opts = question.get("options")
        if qtype == "bool":
            opts = [{"value": True, "i18nKey": question["i18nKey"] + ".yes"},
                    {"value": False, "i18nKey": question["i18nKey"] + ".no"}]
        print(f"\n{label}")
        for i, o in enumerate(opts, 1):
            txt = t(o["i18nKey"], lang) if o.get("i18nKey") in I18N else str(o["value"])
            if qtype == "bool":
                txt = {True: "Si/Yes", False: "No"}[o["value"]]
            print(f"  {i}) {txt}")
        default = question.get("default")
        raw = input(f"> [{default}] ").strip()
        if not raw:
            return default
        idx = int(raw) - 1
        return opts[idx]["value"]
    if qtype == "int":
        default = question.get("default")
        raw = input(f"\n{label} [{default}]: ").strip()
        return int(raw) if raw else default
    if qtype == "multi":
        print(f"\n{label} (coma-separado, enter=defaults)")
        opts = question["options"]
        for i, o in enumerate(opts, 1):
            print(f"  {i}) {t(o['i18nKey'], lang)}")
        raw = input("> ").strip()
        if not raw:
            return [o["value"] for o in opts if o.get("default")]
        picks = [int(x) - 1 for x in raw.split(",") if x.strip().isdigit()]
        return [opts[i]["value"] for i in picks if 0 <= i < len(opts)]
    return question.get("default")


def run(lang="es", out_dir=None):
    print(t("builder.title", lang))
    answers = {"lang": lang}
    for question in MANIFEST["questions"]:
        if not _show(question, answers):
            continue
        answers[question["id"]] = ask(question, lang, answers)
    # helperInterval -> helperIntervalSeconds (nombre que espera helper_compiler)
    if "helperInterval" in answers:
        answers["helperIntervalSeconds"] = answers.pop("helperInterval")
    out_dir = Path(out_dir or (BUILDER / "output"))
    zip_path = build_custom.build(answers, out_dir)
    print(f"\n{t('builder.done', lang)}\n  {zip_path}")
    return zip_path


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Cuestionario interactivo del Stellar Souls Builder.")
    ap.add_argument("--lang", default="es", choices=LANGS)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    run(args.lang, args.out)
