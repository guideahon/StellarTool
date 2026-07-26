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
  "outfitSkinSuit": true,         // bool -> requiere helper
  "miniBoss": "on",               // off | on
  "helperMode": "randomPeriodic", // last | randomAny | randomPeriodic
  "helperIntervalSeconds": 30,
  "lang": "es"                    // es en fr it de ja ko pt_BR ru zh_Hans
}
```

Salida: `StellarSouls-Custom-<hash>.zip` con `Paks\`, `ue4ss\Mods\StellarSoulsOutfitRestore\`
(si hay outfit) e `INSTALL_<lang>.txt`.

## Módulos

- `compiler/helper_compiler.py` — genera config.lua desde respuestas (override de flags).
- `compiler/build_custom.py` — orquestador: resolve preset → copia paks → compila helper → guía → ZIP.
- `features/manifest.json` — catálogo declarativo de preguntas/features.
- `features/preset_map.json` — combo → paks prebuilt (F1).
- `i18n/install/*.txt` — plantillas de guía por idioma.
