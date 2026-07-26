"""Disable the Skin-Suit-on-shield-break outfit swap in an EffectTable, keeping
everything else (combat effects, Tumbler, ss_NoStealth execution-immunity marker,
SS_BurstGaugeReduce, etc.) intact — for the "no shield-break outfit" mini-boss variant.

The Skin Suit is equipped by the `nanosuit_break` effect, whose only action is
`Action1 = EffectAction_AttachEquipment`, `ActionValue1 = BS_102` (the Skin Suit
equipment). Neutralising that action stops the outfit swap on shield break; the
effect stays as an inert infinite no-op, so no other wiring dangles.

Usage:
  python disable_skinsuit_on_break.py <effect-in.json> <effect-out.json>
"""
import json
import sys


def main(inp, outp):
    doc = json.load(open(inp, encoding="utf-8"))
    rows = doc["Exports"][0]["Table"]["Data"]
    r = next((x for x in rows if x["Name"] == "nanosuit_break"), None)
    if r is None:
        raise SystemExit("nanosuit_break not in EffectTable — nothing to disable")

    def setp(name, value):
        p = next((x for x in r["Value"] if x["Name"] == name), None)
        if p is None:
            raise SystemExit(f"nanosuit_break missing {name}")
        before = p.get("Value")
        p["Value"] = value
        p["IsZero"] = value in ("EffectAction_None", None)
        return before

    before_action = setp("Action1", "EffectAction_None")
    before_value = setp("ActionValue1", None)
    json.dump(doc, open(outp, "w", encoding="utf-8"), indent=2)
    print(json.dumps({
        "row": "nanosuit_break",
        "Action1": {"before": before_action, "after": "EffectAction_None"},
        "ActionValue1": {"before": before_value, "after": None},
    }))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
