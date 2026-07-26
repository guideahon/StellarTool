# Stellar Souls Builder — Arquitectura

App **separada** dentro de `Stellar Souls/Builder/`. Reemplaza las ~500 variantes
prebuilt por **un cuestionario** que compila el mod (pak) + helper (Lua) específico
para cada usuario, y arma un ZIP instalable con guía localizada (10 idiomas).

Reusa el toolchain de **Stellar Tool** (`tools/`: retoc, UAssetGUI, repak) y sus
10 archivos `i18n/*.json`. No es un merger — es un **compilador por feature**.

## 1. Principio

Cada feature histórico de todos los mods (Combat, Outfit, Mini-Boss, First Run,
NG+, helper CNS, etc.) se declara UNA vez en `features/manifest.json` como:

- una o más **preguntas** (con claves i18n en los 10 idiomas),
- las **transforms de tabla** que aplica al seleccionarse,
- **dependencias / conflictos** con otras features.

El motor toma las respuestas → resuelve el set de transforms → los aplica sobre
las **tablas base** (JSON UAssetAPI) → `UAssetGUI fromjson` → `retoc`/`repak` →
pak Zen verificado. En paralelo genera `config.lua` del helper. Empaqueta todo en
un ZIP con `INSTALL_<lang>.txt`.

## 2. Pipeline

```
respuestas del cuestionario (QuestionnaireState)
        │  (1) resolver
        ▼
   FeatureResolver  →  lista ordenada de Transforms (deps/conflictos resueltos)
        │  (2) compilar gameplay
        ▼
   TableCompiler  ──  aplica transforms sobre base JSON  (motor = ports de Development/*.py)
        │            UAssetGUI fromjson → retoc to-zen / repak
        ▼
   pak Zen (StellarSouls-Custom_P.{pak,ucas,utoc})  + verificación re-tojson
        │  (3) compilar helper (si aplica)
        ▼
   HelperCompiler  →  StellarSoulsOutfitRestore/ (main.lua fijo + config.lua generado)
        │  (4) empaquetar
        ▼
   ZipPackager  →  Custom_<hash>.zip  { Paks\… , ue4ss\Mods\… , INSTALL_<lang>.txt }
```

## 3. Componentes

| Módulo | Responsabilidad |
|---|---|
| `manifest.json` | catálogo declarativo de features → preguntas + transforms + reglas. |
| `FeatureResolver` | valida respuestas, aplica deps/conflictos, ordena transforms. |
| `TableCompiler` | aplica transforms JSON sobre tablas base; invoca UAssetGUI/retoc/repak. Reusa la lógica de `Development/*.py` (midpoint, buff mini-boss, reward routing, etc.). |
| `HelperCompiler` | escribe `config.lua` desde las respuestas (restoreMode, periodic, intervalos). |
| `ZipPackager` | arma el árbol de instalación + guía localizada + ZIP. |
| `i18n` | reusa `Stellar Tool/i18n/*.json`; el builder agrega su namespace `builder.*`. |

## 4. Tablas base (fuente de compilación)

Viven en `Builder/base_tables/` (JSON UAssetAPI extraídos con retoc). Necesarias:
`SkillTable`, `SkillResultTable`, `CharacterTable`, `EventSpawnTable`,
`RewardGroupTable`, `EffectTable`, más las **vanilla** para diffs/midpoint.
(Hoy presentes en `C:\Temp\ssmod\`; se copian versionadas al repo build.)

> Nota: EffectTable vanilla ~262 MB JSON. El compilador trabaja stream/row-targeted,
> no reescribe estructura (invariante round-trip de Stellar Tool).

## 5. Ejes del cuestionario (ver manifest)

- **Perfil de combate**: ninguno / Full / First Run (half-strength).
- **Outfit SkinSuit on break**: sí/no (define si hace falta helper).
- **Mini-Boss + NG+**: off / Great Desert / todas las regiones (+ densidad).
- **Sub-tweaks combat**: gauge Beta, burst, tachy, blaster cell, turret stagger,
  perfect dodge sin lock-on, tumbler heal, execution immunity. (defaults sanos.)
- **Helper CNS** (solo si outfit): last / random-any / random+periódico (intervalo).

## 6. Fases

1. **F1 (núcleo)**: manifest + FeatureResolver + HelperCompiler + ZipPackager +
   guía localizada. Gameplay = selección de preset prebuilt como fallback. Entrega
   ZIP correcto end-to-end para helper + combos ya existentes.
2. **F2**: TableCompiler real para el eje Combat (SkillTable/SkillResultTable) y
   Outfit (EffectTable on/off) — porta `merge_build.py`/`build_v5.py`.
3. **F3**: Mini-Boss/NG+ compilado (CharacterTable/EventSpawn/RewardGroup) — porta
   `build_allmaps.py`; First Run midpoint — porta `build_first_run_variant.py`.
4. **F4**: UI del cuestionario en los 10 idiomas + verificación post-build + tests.

## 7. Reglas heredadas (de AGENTS de Stellar Tool y memoria del proyecto)

- Nunca parsear binario uasset a mano — todo por UAssetGUI/retoc/repak.
- Round-trip fiel: aplicar sólo cambios sobre JSON base, no regenerar estructura.
- **Nunca filas de item nuevas en SB cocinado** — no registran; usar aliases
  existentes (Gear_*_Rare_MK2 = tope real). MK3+ imposible.
- Execution immunity = repuntar skills a filtros existentes ya marcados, nunca clones.
- Loot enemigo = RewardSpawnBucketType World + formation None + save Save; group Direct+All o Drop+RandomWeight.
