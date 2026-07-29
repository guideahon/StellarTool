# Stellar Souls Builder

Compilador por cuestionario: en vez de ~500 variantes prebuilt, el usuario responde
unas preguntas y obtiene un ZIP instalable con SU mod (pak) + helper (config.lua) +
guía de instalación en su idioma (10 soportados).

Ver [ARCHITECTURE.md](ARCHITECTURE.md). Manifest de features: [features/manifest.json](features/manifest.json).

## Estado

- **F1 (núcleo) — HECHO y probado.** Resolver + HelperCompiler + ZipPackager + guías
  localizadas. Gameplay = selección de preset prebuilt (fallback). Instalador
  funcional end-to-end.
- F2+ = compilación real por cambio. El formulario nativo permite combinar de
  forma independiente daño Beta/Burst, drones, dash, ataques de EVE, daño y
  vulnerabilidad de enemigos, perfect dodge, Tachy, Blaster Cell y extras.
  También genera `StellarSouls-HarderBosses` desde la baseline local de
  `DifficultyStatGroupTable` con presets Main/Insane exclusivamente para
  bosses de Hardcore, con Maelstrom excluido. El perfil nativo sólo modifica
  enemigos normales y los mini-bosses siguen siendo otra opción independiente.
  Perfil/economía/mini-boss se mantienen como grupos exclusivos cuando sus
  valores se superponen.

## Uso (CLI, F1)

```bat
python compiler\build_custom.py --out <carpeta> --answers @answers.json
```

`answers.json`:
```json
{
  "combatProfile": "full",        // none | full | firstRun
  "outfitMode": "helper",         // off | helper | noHelperAlpha
  "miniBoss": "on",               // off | on
  "helperMode": "randomPeriodic", // last | randomAny | randomPeriodic (CNS) | lastNoCns
  "helperIntervalSeconds": 30,
  "vanillaHelperBuild": "off",    // off | alpha1..alpha6 (helper sin CNS, ALPHA)
  "lang": "es"                    // es en fr it de ja ko pt_BR ru zh_Hans
}
```

Salida: `StellarSouls-Custom-<hash>.zip` con `Paks\`, `ue4ss\Mods\StellarSoulsOutfitRestore\`
(si hay outfit) e `INSTALL_<lang>.txt`.

Los extras de gameplay incluyen también el grupo **Base Attribute Enhancement**,
seleccionable por partes: HP/escudo base, reducción y regeneración de escudo,
capacidad Beta/Burst, regeneración pasiva de HP, pesca, munición x100,
recuperación sostenida tras parry/dodge perfecto, cooldown de dash, escaneo del
dron y rotación durante GunGorgon. Las alternativas que pisan los extras
anteriores (munición 999, regeneración 120/30 y recuperación sin skill tree) se
mantienen separadas para no cambiar proyectos guardados.
La curación base del Tumbler es otro check independiente con niveles visibles
de 10% a 100% en pasos de 10% (60% para proyectos anteriores).

### Helper vanilla (ALPHA, sin CNS)

`vanillaHelperBuild` agrega `ue4ss\Mods\StellarSoulsVanillaRestore\` al ZIP: el
mismo `main.lua` con distinta `strategy` en `config.lua`, para averiguar qué
camino de repaint vanilla funciona cuando no hay CNS (caso del QTE de jefe, donde
el escudo se repara sin que nada re-evalúe el outfit). Las builds están en
`compiler/vanilla_helper.py`:

| id | estrategia | necesita |
|----|-----------|----------|
| alpha1 | probe: solo lee y diagnostica | nada — empezar por esta |
| alpha2 | `ApplyMeshInfo()` / `NotifyBP_SetMesh()` | ni cheat manager ni id de traje |
| alpha3 | `SBPlayerEquipItem("NanoSuit", true, id)` | CheatManagerEnablerMod |
| alpha4 | igual, pero construye su propio cheat manager | nada (esquiva el enabler) |
| alpha5 | desequipar + reequipar | cheat manager |
| alpha6 | cadena 2 → 3/4 → 5, para en la que repinta | — |

Son ALPHA: sin confirmar in-game, se instala UNA por vez (comparten nombre de
carpeta) y cada una loguea a `%USERPROFILE%\StellarSoulsVanillaRestore.log`.
Si es lo único seleccionado, el ZIP sale solo con el helper (sin paks).
Los zips sueltos para testers salen de `compiler/build_alpha_helpers.py`.

## Módulos

- `compiler/helper_compiler.py` — genera config.lua desde respuestas (override de flags).
- `compiler/vanilla_helper.py` — tabla de builds ALPHA del helper sin CNS + compilación.
- `compiler/build_custom.py` — orquestador: resolve preset → copia paks → compila helper → guía → ZIP.
- `features/manifest.json` — catálogo declarativo de preguntas/features.
- `features/preset_map.json` — combo → paks prebuilt (F1).
- `i18n/install/*.txt` — plantillas de guía por idioma.
