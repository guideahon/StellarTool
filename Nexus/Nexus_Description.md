Stellar Tool (Mod Merger)

Short description:
Merge Stellar Blade table mods without losing anything: see every value each mod changes (vanilla → modded), pick the changes you want with checkboxes, resolve conflicts line by line, and export one verified merged pak — plus an installable zip for your mod manager. GUI + command line.

Description

Stellar Tool is a mod merger for Stellar Blade, built to solve the classic problem: two mods edit the same table file (SkillTable, CharacterTable, EffectTable...), the game only loads one of them, and you silently lose half of what you installed.

Instead of merging blind, Stellar Tool decodes the DataTables inside each mod and shows you every single change as a readable line:

EffectTable · Item_HP_RPotion · CalculationValue: 60 → 30 (-50%)
SkillTable · M_DroidTurret_Laser · AttackDamageRate: 1.5 → 4.5 (+200%)

You check the changes you want, and when two mods touch the same value with different numbers, the tool flags it as a conflict and lets you pick the winner side by side. Then it rebuilds the tables and packs everything into a single verified container.

Main features

- Load any number of mods: legacy .pak files, zips, loose .uasset folders, AND Zen/IoStore mods (the usual Nexus format, .ucas/.utoc) via CUE4Parse.
- Per-change checkboxes: every modified row/property is one line you can keep or drop. Whole tables can be toggled at once.
- Real before/after values against a vanilla baseline, with percentages, so you know exactly what each mod does.
- Conflict detection at value level (same table + same row + same property with different values). Side-by-side resolution, per conflict or "prefer this mod for everything".
- Non-table assets (meshes, textures) are handled as whole-file replacements with the same check/conflict logic.
- Output is the game's native Zen/IoStore container (zzz_StellarTool_Merged_P.pak/.ucas/.utoc), verified after packing. The zzz prefix guarantees load priority.
- Optional installable .zip (Paks\ + readme) ready to drop into Vortex or any mod manager.
- Every merged table is round-trip verified (re-decoded and re-compared against your selection) before packing — if something does not survive intact, the merge fails loudly instead of corrupting your game.
- Projects (.stproj): save your mod list, selections and conflict resolutions, reopen later.
- Headless mode for scripts/automation:
  StellarTool --headless merge --mod "<A>" --mod "<B>" --out <dir> [--prefer <mod>]
- Your source mods are never modified. The tool only writes the merged output.

Reading Zen/IoStore mods

Most Stellar Blade mods ship as Zen/IoStore containers (.ucas/.utoc). Stellar Tool reads them with CUE4Parse — point it at your Stellar Blade folder once (Settings; it auto-detects Steam) and it can analyze and diff Zen mods against vanilla.

When merging Zen mods, numeric changes (HP, damage, shields, multipliers…) are written back and verified. Non-numeric changes (text, enums, arrays of effects, object references) are shown in the diff but skipped on write — they don't round-trip reliably into a Zen container yet. The tool counts and reports exactly how many were skipped, so nothing is silent.

What it does NOT do

- It does not edit values by hand (yet) — it merges what your mods already change.
- Non-numeric changes from Zen mods aren't written back (see above).

Install

1. Download and extract anywhere.
2. Run StellarTool.exe.
3. Drag your mods in, press Analyze, review changes and conflicts, merge.
4. Copy the generated files to StellarBlade\SB\Content\Paks\~mods (or install the generated zip with your mod manager).
5. IMPORTANT: disable/remove the source mods you merged, so they do not override the merged pak.

Optional but recommended: a vanilla baseline (so you get "vanilla → modded" values instead of raw mod values). The readme on GitHub explains how to extract it from your own game copy in one command.

Requirements and conflicts

- Windows 10/11. No game file is ever touched; the tool only reads mods and writes to the output folder you choose.
- The merged pak conflicts, by definition, with the mods you merged into it — disable them.
- Open source (MIT): https://github.com/guideahon/StellarTool — code, build instructions and issue tracker there. Bug reports with the mods involved are welcome.

Shout outs

- trumank, for repak and retoc — the pak/IoStore tooling this relies on.
- atenfyr, for UAssetGUI / UAssetAPI — the uasset decoding that makes value-level merging possible.
- The Stellar Blade modding community for the mappings (.usmap) groundwork.
- Raxdiam, whose "we're modifying the same file and clashing" description of gameplay mods inspired this tool.
