# Manual de migración de Stellar Souls Builder a CPS

## Objetivo y alcance

La migración deseada es **Stellar Souls Builder → plugin CPS**. Stellar Tool es
la aplicación de escritorio que contiene ese Builder y otras herramientas; el
objetivo no es convertir toda la aplicación Qt en una DLL CPS.

La app desktop debe seguir compilando mods estáticos cuando una opción crea
assets, filas, spawns, cambios complejos o paquetes `.pak`. CPS permite exponer
en el juego las opciones que puedan cambiarse de forma segura en las estructuras
ya cargadas. Las dos vías pueden convivir: Builder genera un mod persistente y
el plugin CPS modifica la sesión actual.

## Estado actual

El plugin separado vive en `CPSPlugin/`. Usa el CPS SDK local del mantenedor,
implementa su página de opciones y empaqueta un `.cps`. La versión actual del
plugin es 0.2.0. Requiere CPS 1.2.0+; el juego todavía debe validar cada ajuste.

### Opciones portadas

Todas las opciones de esta lista operan sobre `CharacterTable / Player` y
validan nombre de propiedad, tipo (`IntProperty` o `FloatProperty`) y tamaño
antes de escribir:

| Control CPS | Campos del juego | Fuente del Builder |
|---|---|---|
| Vida, escudo y reducción de daño | `MaxHP`, `MaxShield`, `DamageReductionPerShieldBock` | `Builder/compiler/extras.py`: `base_attributes` |
| Regeneración de escudo | `ShieldRegenPerSecond`, `ShieldRegenPerSecondWhenBattle` | `shield_regen` |
| Velocidad de ataque | `AttackSpeed` | `attack_speed` / `player_qol` |
| Capacidad Beta, Burst y Tachy | `MaxBetaGauge`, `MaxBurstGauge`, `MaxTachyGauge` | `high_gauge_capacity`, `longer_tachy` |
| Pilas de munición y consumibles | `StackBullet1..6`, `StackConsumable1..7` | `ammo_stacks`, `consumable_stacks`, `player_qol` |
| Regeneración pasiva de vida | `HPRegenPerSecond` | `passive_hp_regen` |
| Curación al golpear | `DrainHpByAttackPowerRate`, `DrainHpFixedValue` | `hp_drain` |
| Poder de pesca | `FishingAttackPower` | `fishing_power` |

Los valores se pueden ajustar desde CPS y se guardan en
`CPS/Configs/StellarToolGameplay/settings.json`. Los grupos se inician apagados.
Al apagarlos, el plugin intenta restaurar el valor que observó antes de
escribir. Solo restaura si el campo conserva el último valor escrito por el
plugin; si otro mod lo cambió, no lo pisa y registra el detalle en `CPS.log`.

## Paso a paso para portar una opción nueva

### 1. Seguir la opción desde la UI hasta su transform

Leer solo los archivos relacionados, en este orden:

1. `Builder/features/manifest.json`: pregunta, opción y transforms declarados.
2. `Builder/compiler/build_specs.py`: tablas y dependencias de cada grupo.
3. `Builder/compiler/table_compiler.py`: registro del transform y tabla base.
4. El módulo concreto, por ejemplo `extras.py`, `skill_extras.py`,
   `effect_extras.py` o `world_extras.py`.
5. `Builder/compiler/build_custom.py`: valores configurables, escalas y cómo
   se combinan los transforms seleccionados.

No asumir que un transform interno es una opción visible del cuestionario: el
`manifest.json` es la fuente de preguntas públicas; `build_specs.py` muestra
también transform IDs internos y dependencias.

### 2. Clasificar la operación antes de escribir código CPS

**Candidata a runtime:** solo cambia valores escalares de filas que ya existen
en una DataTable cargada, y hay una forma de capturar/restaurar el valor
anterior. Ejemplos: int/float de un jugador, multiplicadores o tiempos.

**Mantener en desktop:** crea/quita filas o assets; modifica arrays/structs o
enums; cambia referencias; clona enemigos; reubica spawns; genera materiales,
meshes, outfits o `.pak`; depende del paso de empaquetado UAssetGUI/retoc.

**Requiere prototipo separado:** edita múltiples tablas o tiene dependencias,
por ejemplo una opción que cambia tanto `SkillTable` como `EffectTable`. CPS
debe aplicar todas las partes o mostrarla como incompleta; no ofrecer un toggle
que deje el juego en un estado parcial.

### 3. Definir contrato de datos

Antes de agregar un control, anotar en la tabla de cobertura:

- ID público del manifest y transform interno.
- DataTable exacta, fila(s), propiedad(s) y tipos esperados.
- Fórmula y unidad (por ejemplo porcentaje almacenado como `0.03`).
- Valor por defecto y límites de UI, siguiendo `build_custom.py`.
- Dependencias y si otra feature puede escribir el mismo campo.
- Cuándo vuelve a leer el juego la tabla y si requiere recargar zona/personaje.
- Cómo se restaura si otro plugin escribe después.

No inventar offsets ni parsear `.uasset`: el plugin debe usar las helpers UE
del CPS SDK y reflection. Para filas de DataTable, reutilizar
`ue::data_tables()`, `ue::table_rows()`, `ue::table_struct()` y
`ue::row_field<T>()` solo en el callback del hilo de juego.

### 4. Implementar un control reversible

En `CPSPlugin/src/plugin.cpp`:

1. Agregar la opción y sus valores a `Settings`.
2. Leerlos con validación y límites desde `settings.json`; serializarlos en
   `encode_settings()`.
3. Agregar el target en `make_targets()` con fila y propiedad explícitas.
4. Capturar el valor original antes de la primera escritura.
5. Validar tipo y tamaño exactos; rechazar cualquier valor no finito.
6. En cada cambio de slider/toggle, pasar una copia sincronizada de la
   configuración al callback de game tick. No leer ni escribir UObjects desde
   el callback de dibujo/render.
7. Al apagar, restaurar condicionalmente. Al descargar, usar
   `cps_plugin_unload()` para restaurar desde el hilo de juego.
8. Registrar fallos concretos con tabla/fila/propiedad en CPS Logs.

Para operaciones por filas (como `SkillTable`) hacer una implementación
separada/probada del selector de filas. No ampliar el caso actual de Player con
un barrido indiscriminado de todas las tablas.

### 5. Agregar presentación bilingüe

Todo texto visible va por `api->translate()` y sus archivos:

- `CPSPlugin/locale/en.json`
- `CPSPlugin/locale/es.json`

Actualizar ambas traducciones y comprobar que no se pasen placeholders de
formato como texto literal. En `cps_ui::slider`, el último argumento es el
formato del valor; la etiqueta debe ser estable y no contener el valor mutable.

### 6. Compilar y empaquetar

Desde la raíz del repo:

```bat
CPSPlugin\build.bat
```

El SDK se busca por defecto en `CPS-SDK/`. Si está en otro lugar, configurar
CMake con `-DCPS_SDK_DIR="C:\ruta\CPS-SDK"`. El paquete sale en
`build\CPSPlugin\Release\stellar_tool_gameplay.cps`.

`cps_pack.exe` valida manifest, DLL x64, export `cps_plugin_load` y bundle. El
paquete incluye las licencias/notas necesarias del SDK, no el SDK ni
`cps_pack.exe`.

### 7. Validación dentro del juego

El agente compila y revisa el paquete, pero no debe iniciar ni automatizar el
juego. El usuario prueba copiando el `.cps` a
`SB\Binaries\Win64\CPS\Plugins`, inicia Stellar Blade y abre CPS con F10.

Prueba manual recomendada, un control a la vez:

1. Confirmar que CPS lista **Stellar Tool Gameplay** como cargado.
2. Abrir **Gameplay/Jugabilidad** y verificar el estado del plugin.
3. Activar un solo control, aplicar el cambio dentro de una sesión, comprobar
   si el juego lo refleja y si hace falta recargar personaje/zona.
4. Desactivar el control y comprobar la restauración.
5. Revisar `SB\Binaries\Win64\CPS\CPS.log` si falta tabla/campo o el valor no
   se refleja.
6. Desactivar todo antes de comparar con otras modificaciones. Si el plugin se
   descarga desde CPS, el callback de limpieza también intenta restaurar.

No interpretar que la lista Enabled de mods de Vortex confirme que una función
runtime se aplicó: revisar el estado/log de CPS y el resultado en juego.

## Mapa de Stellar Souls Builder y orden sugerido

| Familia del Builder | Tablas/operación | Estado CPS | Siguiente decisión |
|---|---|---|---|
| Atributos/QoL del jugador | `CharacterTable / Player`, int/float | Portada en la versión actual | Validar efecto/tiempo de aplicación en juego |
| Ventana de parry perfecto y esquive aéreo | `SkillTable`, selección de varias filas y escalares | Pendiente | Próxima familia candidata; respetar filas de `skill_extras.py` |
| Cooldowns de habilidad/drone scan | `SkillTable` y, para scan, también `EffectTable` | Pendiente | Portar primero como una unidad completa; no exponer mitad de `droneScanBoost` |
| Combate y economía | `SkillTable`, `SkillResultTable`, `CharacterTable` | Mayormente estática | Separar cada transform y revisar conflictos de perfiles Full/First Run |
| Outfit / SkinSuit on break | `EffectTable`, arrays y helper CNS | Desktop | No portar hasta diseñar dependencia/integración con CNS y helper |
| No fall/no environment death, gears, recuperación | `EffectTable`; algunos campos son arrays/FName/efectos | Desktop | Requiere soporte tipado, referencias y restauración de arrays |
| Mini-boss, variedad y bosses de overworld | clonado de filas + `EventSpawnTable`/recompensas | Desktop | No es un toggle scalar; requiere generación y verificación de mod |
| Ajustes de mundo | `ShopItemTable`, `RewardGroupTable`, `SPLevelTable`, `CharacterLevelTable`, `ItemFishTable` | Desktop | Futuro candidato si se implementa selector multi-tabla y porcentaje sobre valor capturado |
| Helper CNS | helper Lua, hotkeys, wardrobe events | Desktop/UE4SS | Diseñar port nativo CPS; no copiar el helper a ciegas |
| Movesets/outfits | assets Zen/IoStore y paquetes | Desktop | CPS no reemplaza la generación de `.pak` ni assets cocinados |

Fuentes para confirmar detalles: `Builder/ARCHITECTURE.md`,
`Builder/compiler/extras.py`, `skill_extras.py`, `effect_extras.py`,
`world_extras.py` y `build_specs.py`.

## Handoff para la próxima IA

Al retomar este trabajo:

1. Leer `AGENTS.md`, este manual, `ARCHITECTURE.md` §12 y
   `docs/CPS_PLUGIN.md`.
2. Ejecutar `git status --short --branch`; conservar cambios locales ajenos y
   no asumir que el working tree está limpio.
3. No agregar `CPS-SDK/` al repo: es una copia local que aportó el usuario. No
   versionar el `.cps` de `build/` ni ningún asset del juego.
4. Continuar desde el plugin existente; no rehacer el prototipo ni integrar CPS
   en el proceso Qt desktop.
5. Confirmar que el usuario probó la versión anterior antes de ampliar el
   siguiente grupo si el cambio puede afectar el arranque del juego.
6. Para la siguiente implementación, preferir `forgivingJust` y
   `extraAirDodge` de `Builder/compiler/skill_extras.py`; ambas usan campos
   numéricos de filas existentes de `SkillTable`. Extender el snapshot para
   conservar tabla + fila + propiedad y hacer restauración por valor previo;
   no generalizar a `EffectTable` en el mismo cambio.
7. Actualizar este manual, `docs/CPS_PLUGIN.md`, `ARCHITECTURE.md`, `PLAN.md` y
   `README.md` al añadir cobertura; incrementar `plugin.toml` y volver a generar
   el paquete con `CPSPlugin/build.bat`.
8. Distinguir siempre “compila/empaqueta” de “probado en juego”. Solo el usuario
   puede confirmar el segundo estado en su instalación.
