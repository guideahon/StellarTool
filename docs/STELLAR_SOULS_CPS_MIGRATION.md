# Migrar Stellar Souls Builder a CPS: manual paso a paso

## 1. Objetivo correcto

La solicitud es migrar **Stellar Souls Builder** al menú CPS. Stellar Tool es la
aplicación de escritorio que contiene ese Builder y varias funciones distintas;
no se pretende convertir toda la aplicación Qt en una DLL de CPS.

El criterio es ofrecer en juego los ajustes que puedan hacerse de forma
reversible sobre valores escalares de tablas ya cargadas. El Builder de
escritorio sigue siendo necesario cuando una opción genera `.pak`, assets,
filas, referencias, spawns o archivos helper. Si una opción se migra a medias,
la UI y este manual deben decir qué parte falta.

## 2. Archivos que se consultan primero

En la raíz del repo leer `AGENTS.md`, `README.md` y `ARCHITECTURE.md`; luego:

1. `Builder/features/manifest.json`: catálogo de opciones del cuestionario.
2. `Builder/compiler/build_specs.py`: mapea respuestas a tablas y transforms.
3. `Builder/compiler/table_compiler.py`: transforms de tablas y dependencias.
4. El módulo real de cada transformación: `extras.py`, `skill_extras.py`,
   `effect_extras.py`, `world_extras.py`, `enemy_tweaks.py`,
   `hardcore_enemies.py`, `miniboss_builder.py` o `overworld_bosses.py`.
5. `Builder/compiler/build_custom.py`: valores graduables, normalización,
   empaquetado, helper y composición de presets.
6. `CPSPlugin/src/plugin.cpp`, `CPSPlugin/locale/en.json` y
   `CPSPlugin/locale/es.json`: controles runtime actuales.
7. `CPS-SDK/cpp/ue/tables.hpp` y `ue.hpp`: reflexión que realmente expone el
   SDK local. No inventar APIs a partir de versiones online.

El manifest enumera las opciones públicas, pero `build_custom.py` y la UI
desktop también admiten opciones avanzadas (perfiles de desafío, tuning granular
de enemigos, hardcore, movesets y variantes de assets). Buscar esas rutas antes
de afirmar que el inventario está completo.

## 3. Clasificar cada transformación

Antes de programar, responder:

1. ¿Qué tabla, fila y propiedad toca?
2. ¿Qué tipo tiene la propiedad en el juego: int, float, enum/FName, array,
   struct o referencia?
3. ¿La operación es un valor fijo, una escala sobre el original, o depende de
   otra tabla/baseline?
4. ¿La tabla está cargada en la sesión cuando se aplica? ¿Cuándo vuelve a
   consultarla el juego?
5. ¿Se puede capturar y restaurar sin sobrescribir un cambio de otro mod?
6. ¿El resultado sería equivalente al Builder completo o solo una parte?

**Apta para CPS actual:** escalar existente `IntProperty`/`FloatProperty`, con
fila localizable y valor original capturable.

**Mantener en Builder:** filas/actores/spawns nuevos; arrays/structs; enums,
`FName`, SoftObject references, assets, paquetes, configuraciones Lua/helper o
efectos de outfit dependientes de CNS. Una operación con dos tablas debe migrar
ambas como unidad; no exponer solo una mitad.

## 4. Arquitectura runtime vigente

- Proyecto CMake x64 separado: `CPSPlugin/`.
- SDK local, no versionado: `CPS-SDK/`.
- API CPS 1.2.0 o posterior; reflexión UE 4.26 del SDK.
- Todas las lecturas y escrituras UObject ocurren en `on_game_tick`, hilo del
  juego. Los callbacks UI solo copian/guardan settings.
- Tablas permitidas: `CharacterTable`, `SkillTable`, `EffectTable`,
  `ShopItemTable`, `RewardGroupTable`, `SPLevelTable`,
  `CharacterLevelTable`, `ItemFishTable`.
- Snapshot: objeto de tabla débil + fila + propiedad + tipo/valor original +
  último valor escrito. Al apagar o descargar se restaura solo si el valor
  actual sigue siendo el último escrito por el plugin.
- Escaneo de tablas cada dos segundos para capturar las que aparecen al cargar
  zonas. Si hay más de una tabla homónima, se omite esa tabla y se registra en
  `CPS.log`.
- Configuración: `CPS/Configs/StellarToolGameplay/settings.json`.
- UI en inglés y español. Todo texto se traduce con `CPSPlugin/locale/*.json`.

No ampliar a un barrido indiscriminado de propiedades. Cada transform debe
declarar tabla/filtro/fila/campo y validar tipo, tamaño, rango y finitud antes de
escribir. La UI debe explicar alcances parciales.

## 5. Matriz completa del Builder frente al plugin

### Portado en el plugin 0.3.0

| Opción o grupo | Cobertura runtime | Transform/origen |
|---|---|---|
| Atributos base | `CharacterTable / Player`: vida, escudo y reducción | `extras.py:base_attributes` |
| Regeneración de escudo y velocidad de ataque | `CharacterTable / Player` | `shield_regen`, `attack_speed`, subconjunto de `playerQol` |
| Capacidad Beta/Burst/Tachy | `CharacterTable / Player` con sliders | `high_gauge_capacity`, `longer_tachy` |
| Munición, consumibles, HP pasiva, curación al golpear y pesca | `CharacterTable / Player`; incluye `ammo100x` con valores individuales por tipo de bala | `ammo_stacks`, `ammo100x`, `consumable_stacks`, `passive_hp_regen`, `hp_drain`, `fishing_power` |
| Economía de gauges | Ganancia Beta `-0.5`, capacidades `400/800` | `combat.slowerGain`, `combat.lowerCapacity`, valores de `public_1_2_19_overrides.json` |
| Cooldowns/costos de habilidades | Valores de tuning de 18 filas Beta/Burst | `combat.antiSpamSkill`, overrides públicos |
| Daño Railgun x2 | `SkillTable`, disparos `P_Eve_Gun_ShootRailgun*`, campos de daño existentes | `combat.blasterCellDamageX2` |
| Ventana de parry/esquive | `JustActionTime × slider` para skills existentes `P_Eve_*` con valor positivo | `skill_extras.py:forgiving_just` |
| Esquive aéreo adicional | `UsableCount >= 2` en skills `P_Eve_*Air*Evade*` | `skill_extras.py:extra_air_dodge` |
| Cooldown de dash attack | 4 segundos, tres filas nombradas | `skill_extras.py:dash_cooldown_4s` |
| Reducción de consumo Tachy | `EffectTable / P_Eve_SkillTree_TachyGaugeReduceConsumeRate / CalculationValue = 0.6` | `effect_extras.py:tachy_reduce` |
| Mejores gears | `Gear_*` positivos, excepto `_HitDmgUp`, multiplicador configurable | `effect_extras.py:stronger_gear` |
| Curación Tumbler | `EffectTable / Item_HP_RPotion / CalculationValue`, 10–100 | `effect_extras.py:tumbler_heal` |
| Precios de tiendas | campos `MoneyItemCount1..4` y `Discount_...`, porcentaje editable | `world_extras.py:shop_prices` |
| Cantidades de drop | `ItemMinCount`/`ItemMaxCount` de `RewardGroupTable`, porcentaje editable | parte de `world_extras.py:drop_rates` |
| EXP SP | `SPLevelTable / RequiredSPExp`, porcentaje editable | `world_extras.py:sp_exp` |
| Costos de mejora | `CharacterLevelTable / RequiredItemAmount1/2`, porcentaje editable | `world_extras.py:upgrade_costs` |
| Pesca del mundo | `ItemFishTable / Stamina` y `FightingTime`, porcentajes independientes | parte de `world_extras.py:fishing` |

La curación del Tumbler aparece como gauge/world control en distintas versiones
del formulario; se expone una sola vez porque ambos caminos escriben la misma
fila/campo.

### Parcial o pendiente, con motivo

| Opción | Qué falta y por qué |
|---|---|
| `combatProfile`: none/full/firstRun | Los perfiles combinan numerosos cambios de `SkillTable`, `SkillResultTable` y `CharacterTable`; First Run calcula puntos medios respecto de baseline. Un grupo parcial no equivale al paquete compilado. |
| `combatFeatures`: Beta/Burst, dron, dash, EVE, enemigos | Los transforms copian valores alterados del paquete Full con reglas de selección. CPS no tiene ese baseline estático cargado para preservar equivalencia. `tachyDuration` puede cubrirse con el slider de capacidad, pero no reproduce por sí sola el perfil. |
| `perfectDodge` | Restaura `JustSkillActiveAlias`, una referencia/FName; reflexión actual solo escribe escalares numéricos. La ventana temporal sí está portada por separado. |
| `enemyVulnerability` | Necesita filtrar enemigos normales por `ActorType` y aplicar la escala del perfil. El lector tipado de enum/clasificación no está disponible. |
| `burstGaugeReduce` | Quita condiciones/aliases de arreglos de `EffectTable`; no es escalar. |
| `dropRates` (probabilidad) | `DropRate` solo debe escalarse en filas enum `RandomEach`; modificar `RandomWeight` cambiaría el balance relativo. Se migraron las cantidades, no el porcentaje de probabilidad. |
| `noFallDamage`, `noEnvDeath` | Sustituye acciones enum, borra referencias y cambia efectos/estados. |
| `autoGaugeRecovery` y recuperación en el tiempo | Limpia alias/condiciones y modifica campos de loop y arreglos. No migrado a medias. |
| `harderEnemies`, `harderBosses`, `bossStaggerImmunity`, challenge profiles y Hardcore | Dependen de enums de tipo de actor/dificultad para separar bosses, enemigos normales y Maelstrom; varios también cambian Skill/Reward/Difficulty tables. Sin ese filtro, un toggle escalar podría afectar clases equivocadas. |
| `outfitMode`, `outfitVanillaShieldRegen`, helper CNS/vanilla, helperMode/interval | Depende de arreglos de `EffectTable`, QTE/campamento, eventos de equipamiento y archivos Lua/helper. El runtime CPS actual no integra esos eventos/helper. |
| `miniBoss`, densidad, dificultad, variedad enemiga, `overworldBosses` | Clona filas y aliases, filtra pools/regiones, altera recompensas y modifica `EventSpawnTable`. Requiere un pak generado y verificación de referencias. |
| Movesets y variantes de assets | Pueden cambiar assets y tablas de rutas/comandos; se distribuyen en bundles `.pak`/Zen. |

## 6. Paso a paso para agregar otro control

1. Agrega una línea en esta matriz con el ID público del manifest y la
   propiedad concreta; marca si es total o parcial.
2. Lee la función del transform en su módulo fuente; confirma límites, reglas
   de filtrado y dependencia de baseline. No deduzcas el comportamiento solo
   por el nombre del toggle.
3. Agrega settings con defaults apagados; agrega parseo acotado y serialización
   en `plugin.cpp`.
4. Construye targets explícitos usando table + row + field. Si el valor es un
   multiplicador, calcula siempre desde el `Snapshot.original`, nunca desde el
   valor ya modificado.
5. Lee y escribe solo tipos/tamaños comprobados (`IntProperty` de 4 bytes o
   `FloatProperty` de 4 bytes). Rechaza NaN/infinito y valores fuera de rango.
6. Asegura que apagar y descargar restaure solo si el valor actual coincide con
   `last_written`; si hay conflicto, deja el valor externo intacto y loguea el
   nombre completo de tabla/fila/campo.
7. Expone el control en `draw_page()` y agrega traducciones inglesas/españolas.
   Si es parcial, explica en la misma UI qué no incluye.
8. Actualiza `docs/CPS_PLUGIN.md`, esta matriz, `ARCHITECTURE.md`, `PLAN.md` y
   `README.md` cuando cambie el alcance.
9. Compila con `CPSPlugin\build.bat`; no inicies ni automatices el juego. El
   usuario confirma comportamiento en su instalación.

## 7. Compilar e instalar para una prueba manual

Desde la raíz del repositorio:

```bat
CPSPlugin\build.bat
```

Debe actualizar `build\CPSPlugin\Release\StellarToolGameplay.dll` y producir
`build\CPSPlugin\Release\stellar_tool_gameplay.cps`. Copia el `.cps` a
`SB\Binaries\Win64\CPS\Plugins`; no se instala automáticamente. En el juego,
abre CPS con F10, entra a Gameplay/Jugabilidad y prueba primero un solo control.
Revisa la página Logs de CPS y `SB\Binaries\Win64\CPS\CPS.log`. El paquete se
compila localmente, pero no hay afirmación de prueba dentro del juego hasta que
el usuario lo confirme.

## 8. Handoff para otra IA

1. Lee `AGENTS.md`, `README.md`, `ARCHITECTURE.md` §12, este manual y
   `docs/CPS_PLUGIN.md`.
2. Revisa `git status --short --branch` antes de editar; conserva cambios
   locales ajenos.
3. No agregues `CPS-SDK/`, `.cps` de build, archivos del juego, assets de juego
   ni trazas/logs pesados al repo.
4. Continúa el plugin existente y la matriz de arriba. No reconstruyas la
   aplicación desktop como plugin.
5. Si el usuario reporta fallo, solicita/revisa `CPS.log` y la propiedad/tabla
   concreta. Distingue crash del juego de opción no aplicada.
6. El siguiente trabajo pendiente recomendado es investigar una API segura de
   lectura de enums/FName en el SDK y diseñar soporte transaccional para arrays;
   solo después evaluar probabilidad de drops, perfect dodge y efectos. No usar
   offsets adivinados.
7. Antes de terminar, actualizar los documentos de alcance, compilar el `.cps`,
   revisar el diff y respetar los cambios ajenos. No se puede declarar prueba en
   juego desde el agente.
