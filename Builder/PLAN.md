# Stellar Souls Builder — Plan de implementación (F2→F4)

Estado base: F1 completo+tests; F2 pipeline probado **byte-idéntico** en CombatOnly
(SkillTable+CharacterTable). Método de paridad: compilar → cue4parse-extract →
diff vs shipped (ambos vía cue4parse). Toolchain determinista confirmado.

## Estado (2026-07-22): It.1-8 implementadas

- It.1 Outfit ✅ content-idéntico · It.2 multi-pak ✅ · It.3 wire compile-mode ✅ byte-idéntico combat
- It.4 Mini-Boss ✅ compila+verifica+wired (301 clones content-exactos, staging v131)
- It.5 First Run ✅ wired (repack staging v14) · It.6 gaugeTweaks ✅ (baked en base + blasterX2 componible)
- It.7 base_tables portables ✅ (paths.json env-overridable) · It.8 cuestionario ✅ (CLI 10 idiomas -> compila)

Pendiente fino: byte-parity exacta mini-boss/FR (staging interno != publico por delta menor); UI QML nativa en Stellar Tool (hoy CLI questionnaire.py).

## Estado (2026-07-26): selección granular nativa

- UI QML integrada sin dropdowns: cambios independientes con check y valores
  superpuestos con radios visibles de selección única.
- Economía Beta/Burst separada en tres checks: velocidad de recarga, capacidad
  máxima y cooldown.
- SkillTable granular: Beta/Burst, drones, dash, EVE, enemigos y perfect dodge.
- CharacterTable granular: Tachy, vulnerabilidad y economía Beta/Burst.
- EffectTable granular: extras y Tumbler.
- Los presets históricos siguen aceptándose para compatibilidad con plantillas.

## Iteraciones

### It.1 — Outfit EffectTable (paridad)
- Identificar source EffectTable que construyó `DirectRestore-NoRestFX_P` 1.2.19.
- Registrar transform `outfit.effectTable.skinSuitOnBreak` (base full = skinsuit ET).
- Compilar pak `StellarSouls-DirectRestore-NoRestFX` y validar byte-idéntico vs shipped.
- Extraer overrides si difiere (como se hizo en combate).

### It.2 — Multi-pak split
- `compile_pak` acepta varios "targets" (pak_name -> lista de tablas), no un solo pak.
- Combat+Outfit = 2 paks (CombatOnly {SkillTable,CharacterTable} + DirectRestore {EffectTable}).
- Mini-boss = 1 pak combinado.

### It.3 — Wire compile-mode en build_custom
- Resolver mapea combo -> lista de (pakName, [transforms]) en vez de copiar preset.
- Fallback a preset solo si el combo no tiene transforms.
- Helper + guía + zip sin cambios.

### It.4 — Mini-Boss / NG+
- Portar `build_allmaps.py`: CharacterTable clones + EventSpawnTable subset +
  RewardGroupTable + SkillResultTable turret + TargetFilterTable + execution filters.
- Densidad como parámetro (10/20/33%). Región (Great Desert / all).
- Validar byte-idéntico vs `MiniBossNGPlus-Combat_P` 1.31.1.

### It.5 — First Run
- Portar `build_first_run_variant.py`: midpoint + tumbler restore.
- Validar vs First Run pak 1.5.0.

### It.6 — gaugeTweaks individuales
- betaGaugeReduce / burstGaugeReduce / blasterCellX2 / tumblerHeal como toggles.
- Componibles sobre combat.skill.full.

### It.7 — base_tables portables
- Vendorizar tablas base versionadas, o extraer del juego del user en 1er arranque
  (retoc to-legacy sobre paks vanilla + UAssetGUI tojson). Quitar rutas absolutas.

### It.8 — UI QML (F4)
- Página cuestionario en Stellar Tool (o app nueva), 10 idiomas, preview del plan,
  progreso de compilación, resultado + zip. Verificación post-build.

## Invariantes
- Paridad = byte-idéntico vs shipped (validar cada pak como en CombatOnly).
- Round-trip fiel; nunca filas/items nuevos en SB cocinado.
- Comparar contenido SIEMPRE vía cue4parse en ambos lados (no JSON crudo).
