"""Revert Beta/Burst gauge cost to vanilla on a built SkillTable JSON.

Inverse of `apply_beta_burst_energy.js`. For every row whose UseEnergyType is a
Beta/Burst gauge, restore UseEnergyAmount to the vanilla value (looked up by row
name from the vanilla SkillTable). This fixes the HUD "ready" prompt desync
(prompt fires at the vanilla 2-bar threshold while cost was doubled to 4 bars)
WITHOUT touching the -50% Beta/Burst damage nerf, which stays intact.

Handles full-double (800/1200/1600) AND First Run midpoint (600/900/...) uniformly
because it maps back to the exact vanilla amount, not by dividing.

Usage:
  python revert_beta_burst_cost.py <skill-in.json> <skill-out.json> <vanilla-skill.json>
"""
import json
import sys

GAUGE_TYPES = {"SkillEnergyType_BetaGauge", "SkillEnergyType_BurstGauge"}


def rows(doc):
    return doc["Exports"][0]["Table"]["Data"]


def prop(row, name):
    return next((p for p in row["Value"] if p["Name"] == name), None)


def get(row, name):
    p = prop(row, name)
    return p.get("Value") if p else None


def apply_to_doc(doc, van):
    """Edita el doc en memoria contra la SkillTable vanilla ya parseada.

    El compilador lo llama directo: lanzar un python aparte obligaba a escribir
    y releer el JSON de la tabla entera.
    """
    van_amount = {}
    for r in rows(van):
        p = prop(r, "UseEnergyAmount")
        if p is not None:
            van_amount[r["Name"]] = p.get("Value")

    changes = []
    for r in rows(doc):
        etype = get(r, "UseEnergyType")
        if etype not in GAUGE_TYPES:
            continue
        p = prop(r, "UseEnergyAmount")
        if p is None:
            continue
        cur = p.get("Value")
        target = van_amount.get(r["Name"])
        if target is None:
            raise RuntimeError(f"No vanilla UseEnergyAmount for {r['Name']}")
        # normalise numeric compare (vanilla decodes as float e.g. 400.0)
        try:
            same = float(cur) == float(target)
        except (TypeError, ValueError):
            same = cur == target
        if same:
            continue
        p["Value"] = target
        p["IsZero"] = float(target) == 0.0 if isinstance(target, (int, float, str)) and str(target) not in ("+0",) else False
        changes.append({"row": r["Name"], "before": cur, "after": target})

    return {"reverted": len(changes), "changes": changes}


def main(inp, outp, vanilla):
    doc = json.load(open(inp, encoding="utf-8"))
    van = json.load(open(vanilla, encoding="utf-8"))
    report = apply_to_doc(doc, van)
    json.dump(doc, open(outp, "w", encoding="utf-8"), indent=2)
    print(json.dumps({"input": inp, "output": outp, **report}, indent=2, default=str))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3])
