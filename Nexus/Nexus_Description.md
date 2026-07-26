Stellar Tool — mod merger for Stellar Blade

Short description:
Merge Stellar Blade table mods (SkillTable, CharacterTable, EffectTable...) without losing anything, and BUILD your own Stellar Souls mod from a questionnaire. Reads Zen/IoStore mods too. See every change, resolve conflicts, edit values, export one verified merged pak + installable zip. Simple one-click mode and an advanced workbench. GUI and CLI. Open source.

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

When merging Zen mods, numbers, text, enums, arrays and object fields (HP, damage, shields, multipliers, skill aliases, actor states, combo lists...) are written back and verified. The one thing still skipped on write is adding or removing whole rows - those don't round-trip into a Zen container yet - and the tool reports exactly how many were skipped. Nothing is silent.

If a game update makes the bundled mappings stale, Settings -> Mappings can download the matching .usmap for your game version in one click, or load your own - no need to wait for a new release.

When something does go wrong, it says why: the underlying tools can fail without printing anything, so Stellar Tool recovers the real error and writes a diagnostic log to %LOCALAPPDATA%\StellarTool\logs\. Attach the newest one when reporting an issue.

Main features

- Load any number of mods: Zen/IoStore (.ucas/.utoc), legacy .pak, .zip, or loose .uasset folders.
- Real vanilla -> modded values with percentages, per row and property.
- Per-change checkboxes; toggle whole tables at once; edit the final value by hand.
- Bulk edit: apply an operation (xN, +, -, clamp, set...) to many numeric values at once, filtered by a row-name regex - e.g. "x3 all enemy HP" without editing each row.
- Readable TOML patches: export the selected changes as .toml files (row/property = value) for review or sharing, and import a literal .toml patch back as changes.
- Value-level conflict detection and side-by-side resolution.
- Output is the game's native Zen container (zzz_StellarTool_Merged_P), round-trip verified after packing - if a table wouldn't survive intact, the merge fails loudly instead of corrupting your game.
- Optional installable .zip (Paks\ + readme) for Vortex or any mod manager, plus a merge_report.txt.
- Projects (.stproj): save your mod list, selections and resolutions; reopen later.
- Vanilla baseline built from your own game copy in one click; auto-rebuilds if the game updates.
- Headless CLI for scripts/automation. 10 interface languages.
- Your source mods are never modified.

Build mod — make your own Stellar Souls mod (no merging needed)

Beyond merging, Stellar Tool now includes a "Build mod" tab: a questionnaire that COMPILES a custom Stellar Souls mod + its helper for you, instead of picking between dozens of prebuilt variants. Answer a few questions and it builds the exact pak + CNS helper + install guide in your language, byte-for-byte matching the released files.

- Combat / Outfit / Combat+Outfit, Full or First Run.
- Mini-bosses + NG+ gear drops: all regions or Great Desert, adjustable density, progressive difficulty (denser late-game). Anti-farm: respawnable spawns are excluded so they can't be exploited. (BETA)
- Enemy variety: elites and unique foes injected into repetitive zones, on existing spawn points. (BETA)
- Gameplay extras (BETA): Player QoL (high ammo/consumable stacks, shield regen, attack speed), HP Drain, longer Tachy, harder enemies x2-x6, no fall damage, no water/sand death, less Tachy drain, stronger gear x2, Beta on perfect parry / Burst on perfect dodge without the skill tree, a wider perfect parry/dodge window, and a double air dodge.
- Custom TOML patches: drop your own <Table>.toml files (row/property patches) and they're applied on top - a simple declarative format inspired by automod. (BETA)
- CNS outfit helper compiled to your choice: restore last outfit / random / random + periodic swap.
- Auto-detects your game and, with your approval, installs the mod (to ~mods) and the helper (edits mods.txt) directly - and can uninstall exactly what it installed.
- History of every build: reuse a past config as a template, or re-export its zip.
- 10 languages, light/dark theme, and an optional old-school keygen chiptune while you build.

The Build mod tab needs Python - it ships an embedded Python, so you don't install anything. It uses Oodle (oo2core) directly from your own game install to pack (never redistributed). The merger works without any of this.

Install

1. Download and extract anywhere. Self-contained; no installer, no game files touched.
2. Run StellarTool.exe. In Settings, confirm your Stellar Blade folder (auto-detected).
3. Drop your mods in, review, and Merge everything.
4. Install the result to StellarBlade\SB\Content\Paks\~mods (or the generated .zip via your mod manager).
5. Disable the source mods you merged so they don't override the merged pak (one-click button).

Requirements and conflicts

- Windows 10/11. The merger never touches a game file.
- The Build mod tab needs Stellar Blade installed (it reads Oodle from your game to pack). Embedded Python is included; nothing to install.
- The merged pak conflicts, by definition, with the mods you merged into it - disable them.
- Open source (MIT): https://github.com/guideahon/StellarTool - code, build instructions and issue tracker. You can compile it yourself.

What's bundled

StellarTool.exe plus open-source community CLI tools it drives, unmodified: repak and retoc (trumank), UAssetGUI (atenfyr), cue4parse (CUE4Parse CLI). The Build mod tab also bundles an embedded Python and the base tables it compiles from. Oodle (oo2core) is never bundled - it is read from your own game. All auditable from the repo.

Shout outs

- trumank - repak and retoc.
- atenfyr - UAssetGUI / UAssetAPI.
- FabianFG and the FModel team - CUE4Parse, which lets Stellar Tool read Zen mods.
- jpabscale - automod, whose declarative TOML property-patches, regex row selection and versioned mappings inspired the TOML patch support, bulk edit and the .usmap downloader.
- TheNaeem - the Unreal Mappings Archive, which the in-app .usmap downloader pulls from.
- The Stellar Blade modding community - the mappings (.usmap) groundwork, plus FengYeLy and yadilloH for testing and feedback (yadilloH's UAssetGUI report pinpointed the mappings bug behind the merge failures).
- Raxdiam, whose "we're all modifying the same file and clashing" description inspired this tool.
