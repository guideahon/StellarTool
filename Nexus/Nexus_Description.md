# Stellar Tool — Table mod merger for Stellar Blade

## Description

Stellar Tool is a mod merger for Stellar Blade, built to solve the classic problem: two mods edit the same table file (`SkillTable`, `CharacterTable`, `EffectTable`...), the game only loads one of them, and you silently lose half of what you installed.

Instead of merging blind, Stellar Tool decodes the DataTables inside each mod and shows every change as a readable line:

```text
EffectTable · Item_HP_RPotion · CalculationValue: 60 → 30 (-50%)
SkillTable · M_DroidTurret_Laser · AttackDamageRate: 1.5 → 4.5 (+200%)
```

Select the changes you want. When two mods touch the same value with different numbers, the tool flags a conflict and lets you choose the winner side by side. It then rebuilds the selected tables and packs everything into one verified container.

## Two ways to use it

- **Simple:** drop in your mods, click **Merge everything**, and let the tool resolve conflicts by order (the first mod wins).
- **Advanced:** use the full workbench with mod priority, per-change checkboxes, side-by-side conflict resolution, manual final-value editing, bulk numeric edits, and savable projects.

## It shows you exactly what each mod does

Every modified value can be compared against a vanilla baseline:

```text
CharacterTable · ATL_M_Maelstrom_01 · MaxHP: 100000 → 300000 (+200%)
EffectTable · Item_HP_RPotion · CalculationValue: 60 → 30 (-50%)
SkillTable · M_DroidTurret_Laser · AttackDamageRate: 1.5 → 4.5 (+200%)
```

Identical changes from different mods are collapsed automatically. Conflicts are detected at value level — same table, row, and property with different values — and can be resolved individually or by choosing **prefer this mod for everything**.

Non-table assets such as meshes and textures are handled as whole-file replacements with the same check and conflict logic.

## Reads Zen/IoStore mods

Most Stellar Blade mods use Zen/IoStore containers (`.ucas`/`.utoc`). Stellar Tool reads them with CUE4Parse. Point it at your Stellar Blade folder once from Settings; Steam is detected automatically. Legacy `.pak`, `.zip`, and loose `.uasset` folders are supported too.

When merging Zen mods, numeric changes such as HP, damage, shields, and multipliers are written back and round-trip verified. Non-numeric changes such as text, enums, arrays, and object references are shown in the diff but currently skipped during writing because they do not reliably round-trip into a Zen container yet. The tool reports exactly how many changes were applied and skipped.

## Main features

- Load any number of legacy `.pak` files, `.zip` archives, loose `.uasset` folders, or Zen/IoStore mods (`.ucas`/`.utoc`).
- See real vanilla → modded values with percentages, per row and property.
- Select individual changes or toggle complete tables.
- Edit final numeric values by hand or apply bulk operations (`xN`, `+`, `-`, clamp, set...) filtered by row-name regex.
- Detect and resolve conflicts at value level, side by side.
- Handle non-table assets as whole-file replacements.
- Export selected changes as readable TOML patches, or import a literal TOML patch back as changes.
- Output the game's native Zen/IoStore container: `zzz_StellarTool_Merged_P`.
- Round-trip verify every written table after packing; failed verification is reported instead of silently producing a corrupt merge.
- Generate an optional installable `.zip` (`Paks\` plus readme) for Vortex or any mod manager.
- Include `merge_report.txt` with applied changes, skipped changes, and conflict resolutions.
- Save mod lists, selections, and conflict resolutions as `.stproj` projects.
- Build a vanilla baseline from your own game copy in one click and rebuild it when the game updates.
- Use the headless CLI for scripts and automation.
- Your source mods are never modified; only the chosen output folder is written.

## Install

1. Download and extract Stellar Tool anywhere. It is self-contained and has no installer.
2. Run `StellarTool.exe` and confirm your Stellar Blade folder in Settings.
3. Drop in your mods, press **Analyze**, review the changes and conflicts, then merge.
4. Copy the generated files to `StellarBlade\SB\Content\Paks\~mods`, or install the generated `.zip` with your mod manager.
5. Disable or remove the source mods you merged so they do not override the merged pak. Stellar Tool offers a one-click option for this.

A vanilla baseline is optional but recommended. It lets you see vanilla → modded values instead of only the raw values supplied by each mod. The GitHub readme explains how to extract one from your own game copy.

## Requirements and conflicts

- Windows 10/11.
- The tool never modifies game files; it only reads mods and writes to the output folder you choose.
- The merged pak conflicts by definition with the source mods used to create it. Disable those source mods.
- Open source under the MIT license: [github.com/guideahon/StellarTool](https://github.com/guideahon/StellarTool).

## What's bundled

The download includes `StellarTool.exe` and the open-source community CLI tools it drives, unmodified: repak and retoc (trumank), UAssetGUI/UAssetAPI (atenfyr), and cue4parse (CUE4Parse CLI). Everything is auditable from the repository.

## Shout outs

- trumank — repak and retoc, the pak/IoStore tooling this relies on.
- atenfyr — UAssetGUI/UAssetAPI, the uasset decoding that makes value-level merging possible.
- FabianFG and the FModel team — CUE4Parse, which lets Stellar Tool read Zen mods.
- The Stellar Blade modding community — the mappings (`.usmap`) groundwork.
- Raxdiam, whose description of gameplay mods as “we're all modifying the same file and clashing” inspired this tool.
