Stellar Tool — mod merger for Stellar Blade

Short description:
Merge Stellar Blade table mods (SkillTable, CharacterTable, EffectTable...) without losing anything. Reads Zen/IoStore mods too. See every change, resolve conflicts, edit values, and export one verified merged pak + installable zip. Simple one-click mode and an advanced workbench. GUI and CLI. Open source.

Description

Two mods edit the same table, the game loads only one, and you silently lose half of what you installed. Stellar Tool fixes that: it reads the DataTables inside your mods, shows you every change, lets you resolve conflicts, and builds one merged pak that keeps them all.

Two ways to use it

- Simple: drop all your mods, click "Merge everything", done. Conflicts are auto-resolved by order (first mod wins), the result installs straight to your ~mods, and a button offers to disable the original mods so they don't override the merge.
- Advanced: a full workbench to build a merge-mod — mod priority, every change as a checkbox, side-by-side conflict resolution, manual value editing, and savable projects.

It shows you exactly what each mod does

Instead of merging blind, every modified value is one readable line, compared against vanilla:

CharacterTable · ATL_M_Maelstrom_01 · MaxHP: 100000 -> 300000 (+200%)
EffectTable · Item_HP_RPotion · CalculationValue: 60 -> 30 (-50%)
SkillTable · M_DroidTurret_Laser · AttackDamageRate: 1.5 -> 4.5 (+200%)

Tick the changes you want. When two mods set the same value differently, that's a conflict - pick the winner side by side, or "prefer this mod for everything". Identical changes from different mods are collapsed into one line automatically.

Reading Zen/IoStore mods

Most Stellar Blade mods ship as Zen containers (.ucas/.utoc). Stellar Tool reads them with CUE4Parse - point it at your Stellar Blade folder once (it auto-detects Steam) and it can analyze and diff Zen mods against vanilla. Legacy .pak, .zip and loose .uasset folders work too.

When merging Zen mods, numeric changes (HP, damage, shields, multipliers...) are written back and verified. Non-numeric changes (text, enums, arrays, object references) are shown in the diff but skipped on write - they don't round-trip reliably into a Zen container yet - and the tool reports exactly how many were skipped. Nothing is silent.

Main features

- Load any number of mods: Zen/IoStore (.ucas/.utoc), legacy .pak, .zip, or loose .uasset folders.
- Real vanilla -> modded values with percentages, per row and property.
- Per-change checkboxes; toggle whole tables at once; edit the final value by hand.
- Value-level conflict detection and side-by-side resolution.
- Output is the game's native Zen container (zzz_StellarTool_Merged_P), round-trip verified after packing - if a table wouldn't survive intact, the merge fails loudly instead of corrupting your game.
- Optional installable .zip (Paks\ + readme) for Vortex or any mod manager, plus a merge_report.txt.
- Projects (.stproj): save your mod list, selections and resolutions; reopen later.
- Vanilla baseline built from your own game copy in one click; auto-rebuilds if the game updates.
- Headless CLI for scripts/automation. 10 interface languages.
- Your source mods are never modified.

Install

1. Download and extract anywhere. Self-contained; no installer, no game files touched.
2. Run StellarTool.exe. In Settings, confirm your Stellar Blade folder (auto-detected).
3. Drop your mods in, review, and Merge everything.
4. Install the result to StellarBlade\SB\Content\Paks\~mods (or the generated .zip via your mod manager).
5. Disable the source mods you merged so they don't override the merged pak (one-click button).

Requirements and conflicts

- Windows 10/11. No game file is ever touched.
- The merged pak conflicts, by definition, with the mods you merged into it - disable them.
- Open source (MIT): https://github.com/guideahon/StellarTool - code, build instructions and issue tracker. You can compile it yourself.

What's bundled

StellarTool.exe plus open-source community CLI tools it drives, unmodified: repak and retoc (trumank), UAssetGUI (atenfyr), cue4parse (CUE4Parse CLI). All auditable from the repo.

Shout outs

- trumank - repak and retoc.
- atenfyr - UAssetGUI / UAssetAPI.
- FabianFG and the FModel team - CUE4Parse, which lets Stellar Tool read Zen mods.
- The Stellar Blade modding community - the mappings (.usmap) groundwork.
- Raxdiam, whose "we're all modifying the same file and clashing" description inspired this tool.
