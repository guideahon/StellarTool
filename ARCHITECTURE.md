# Stellar Tool — Arquitectura

Herramienta de escritorio (Windows) para inspeccionar y **mergear mods de Stellar Blade**, con UI en **Qt 6 / QML** (mismo stack y convenciones que LlamaCode: core en C++ bajo `src/core`, UI en `qml/pages` + `qml/components`).

Versión actual: **0.6.3**.

---

## 1. Contexto del dominio

- Los mods de Stellar Blade son archivos **`.pak` de Unreal Engine 4.26** (legacy V11) o contenedores **Zen/IoStore** (`.ucas/.utoc`, el formato típico de Nexus).
- Los cambios de gameplay viven en **DataTables**: assets `.uasset` + `.uexp` (ej. `SB/Content/Local/Data/CharacterTable.uasset`). Cada DataTable es un mapa `RowName -> Struct` con propiedades tipadas (float, int, bool, FName, structs anidados, arrays).
- **Conflicto**: dos mods incluyen el mismo asset (misma ruta dentro del pak). El motor carga uno solo (por orden alfabético del pak). El merge real requiere fusionar a nivel **fila/propiedad** y reempaquetar un único pak.

---

## 2. Pipeline de datos

```
mod.zip / carpeta / mod.pak / mod Zen (.utoc)
        │  (1) Ingesta
        ▼
   Extracción .pak  ──────────  repak.exe (legacy)
        │                     └─ retoc to-legacy (Zen → legacy, si es convertible)
        │                     └─ cue4parse.exe (Zen no convertible → JSON clean)
        ▼
   uasset → JSON  ────────────  UAssetGUI.exe tojson (CLI externa)
        │  (3) JSON por tabla (formato UAssetAPI)
        ▼
   Parser DataTable (C++)  →  modelo interno: Table / Row / Property
        │  (4) diff
        ▼
   Diff vs baseline (vanilla) y diff cruzado entre mods
        │  (5) selección del usuario (checks + resolución de conflictos)
        ▼
   Motor de merge → JSON final por tabla
        │  (6) JSON → uasset
        ▼
   UAssetGUI.exe fromjson  →  retoc to-zen / repak pack  →  pak de salida
```

### Herramientas externas (bundled en `tools/`)
| Tool | Uso | Invocación |
|---|---|---|
| `repak.exe` | unpack/pack de `.pak` legacy (UE4.26, version v11) | `repak unpack`, `repak pack --version V11` |
| `retoc.exe` | Zen/IoStore: extraer vanilla del juego, empaquetar merge, verificar | `retoc to-legacy`, `retoc to-zen`, `retoc verify` |
| `UAssetGUI.exe` | `.uasset` ↔ JSON (motor UAssetAPI) | `UAssetGUI tojson <uasset> <json> VER_UE4_26`, `fromjson` |
| `cue4parse.exe` | Leer contenedores Zen/IoStore que retoc no puede revertir | `cue4parse extract --export-json` |

Ambas se invocan vía `QProcess` con timeout, captura de stdout/stderr y verificación de exit code. Nunca se parsea binario uasset a mano.

### Baseline (vanilla)
Para poder decir "este mod cambió X de 100 a 250" hace falta la tabla **original**. Estrategias, en orden:
1. **Cache local de baseline**: primera vez, la tool extrae las tablas vanilla desde los paks del juego (ruta Steam configurable) usando CUE4Parse. Exporta solo las tablas que hacen falta bajo demanda (`ensureTables`), no barre el juego entero.
2. **Baseline importada**: el usuario apunta a un dump JSON de tablas vanilla (se documenta cómo generarlo).
3. **Sin baseline (degradado)**: la tool muestra los valores del mod sin "antes/después", y el diff se hace solo **entre mods** (suficiente para detectar y resolver conflictos).

El diff mod-vs-mod **nunca** requiere baseline: conflicto = misma tabla + misma fila + misma propiedad con valores distintos.

La baseline detecta staleness comparando tamaño+fecha de `pakchunk0` contra un stamp guardado. Las tablas confirmadas como inexistentes en vanilla se cachean para no intentar exportarlas de nuevo.

---

## 3. Modelo de datos (C++ core)

```
ModPackage            // un mod cargado (zip/carpeta/pak/zen)
 ├─ id, name, sourcePath, loadOrder
 ├─ assets: QList<ModAsset>
 └─ zenAssetsNotMerged: QStringList   // assets Zen no convertibles
ModAsset              // un uasset dentro del mod
 ├─ gamePath          // "SB/Content/Local/Data/CharacterTable.uasset"
 ├─ kind              // DataTable | Other | Unreadable
 ├─ cleanJson: bool   // JSON viene de CUE4Parse (valores limpios)
 └─ table: DataTableDoc?
DataTableDoc          // JSON parseado (QJsonDocument retenido para round-trip)
 └─ rows: QMap<QString /*RowName*/, QJsonObject>
ChangeItem            // unidad seleccionable ("check")
 ├─ modId, tablePath, rowName, propertyPath   // propertyPath con notación K:/N:/I:
 ├─ baseValue?  (si hay baseline)
 ├─ newValue
 ├─ changeType       // Modified | RowAdded | RowRemoved | AssetReplaced
 ├─ selected: bool
 ├─ clean: bool      // valor de CUE4Parse (write-back solo escalares)
 ├─ edited: bool     // modificado a mano por el usuario
 ├─ dup: bool        // duplicado exacto de otro mod (oculto)
 └─ conflictGroup?   // id compartido entre ChangeItems que colisionan
ConflictGroup
 ├─ key (tablePath+rowName+propertyPath)
 ├─ candidates: QList<ChangeItem*>   // uno por mod
 └─ resolution: modId elegido
MergePlan             // snapshot serializable (JSON) de selecciones + resoluciones
```

Claves de diseño:
- El **round-trip es JSON-fiel**: al mergear se parte del JSON baseline (o del JSON del primer mod) y se aplican solo los `ChangeItem` seleccionados, sin regenerar estructura. Minimiza riesgo de corromper el uasset.
- `propertyPath` es la identidad estable de un cambio. El diff recursivo sobre el JSON de UAssetAPI compara por `Name` de propiedad, no por índice, salvo en arrays puros.
- Assets no tabulares (meshes, texturas, animaciones) se tratan como **AssetReplaced**: check todo-o-nada, conflicto = elegir un mod entero para ese asset.
- **Normalización**: `normalizeDataTableDoc` convierte cualquier JSON (UAssetAPI o CUE4Parse) a una forma canónica limpia para comparar valores reales sin importar el parser usado.

---

## 4. Módulos C++ (`src/core`)

| Módulo | Responsabilidad |
|---|---|
| `PakService` | wrapper QProcess de repak/retoc: unpack/pack legacy, to-legacy/to-zen de contenedores IoStore, compat-global para revertir mods Zen, extracción de zips. |
| `UAssetService` | wrapper QProcess de UAssetGUI: tojson/fromjson, con detección de versión y reporte de fallos por asset. |
| `Cue4Service` | wrapper QProcess de cue4parse.exe: lee contenedores Zen/IoStore que retoc no puede revertir, exportando DataTables a JSON. |
| `BaselineManager` | cache de tablas vanilla en `%LOCALAPPDATA%/StellarTool/baseline/`, extracción bajo demanda desde el juego (CUE4Parse) o importación. Detecta staleness y tablas ausentes. |
| `ModImporter` | orquesta ingesta: extrae, convierte, clasifica assets, produce `ModPackage`. Maneja mods legacy y Zen (con fallback a CUE4Parse). Corre en `QtConcurrent` con progreso. |
| `TableDiffEngine` | diff JSON recursivo: mod vs baseline → `ChangeItem[]`; cruza mods → `ConflictGroup[]`. |
| `MergeEngine` | aplica `MergePlan` sobre JSON base, invoca fromjson + pack, valida resultado (re-tojson y re-diff de verificación). Rewrites enums numerados. |
| `CnsConverterService` | conversor nativo C++ replacer ↔ CNS: descubre raíces con sus bases de rutas, reubica dependencias, reescribe referencias UAssetAPI y empaqueta con retoc. |
| `CnsIdFixerService` | escanea mods IoStore, corrige `Container_Id` duplicados con backup verificable, reporta conflictos de `Package_Id`. |
| `LiveService` | control en vivo del juego (fase 1: FOV, velocidad, salto). Instala el bridge Lua propio `StellarToolLive` en los mods de UE4SS y habla con él por archivos de texto atómicos. No inyecta código ni lee memoria. |
| `ReShadePresetService` | detecta `SB/Binaries/Win64/ReShade.ini`, mantiene la biblioteca de presets en `%LOCALAPPDATA%/StellarTool/reshade/`, importa/exporta y restaura copias administradas en el juego con backup previo. Comprueba de forma best-effort los shaders referenciados. |
| `SaveConverterService` | fachada async para el conversor de partidas de lotress: `.sav` ↔ JSON y reparación CNS, ejecutado con el Python embebido del paquete Builder. |
| `UsmapService` | descarga mappings versionados desde el archivo de la comunidad, lee enums del usmap, detecta versión del juego. |
| `GamePaths` | autodetección de instalación Steam, normalización de ruta, gestión de stages temporales para CUE4Parse. |
| `TomlPatch` | parser/serializador seguro de patches TOML estilo automod: metadata, operaciones declarativas, regex de filas y bundles. Nunca ejecuta patchlets. |
| `ProjectStore` | guarda/carga sesión (`.stproj` JSON): mods cargados, selecciones, resoluciones. |
| `UpdateService` | autoactualización desde GitHub Releases: chequea, descarga, extrae y relanza. |
| `AppController` | fachada QObject expuesta a QML (patrón LlamaCode). Modelos: `ModListModel`, `ChangeListModel` (por tabla, con roles para check/conflicto), `ConflictModel`. |
| `HeadlessRunner` | modo CLI sin UI: toda función de la app accesible por línea de comandos. |

---

## 5. UI (QML)

Páginas (`qml/pages`):
1. **HomePage** — drop zone (zip/carpeta/pak), lista de mods cargados con orden, botón "Analizar".
2. **ChangesPage** — árbol: Mod → Tabla → Fila → Propiedad. Cada hoja con CheckBox, texto resumen ("`CharacterTable / EVE / MaxHP: 100 → 250`"), badge de conflicto. Filtros: solo conflictos / por tabla / búsqueda.
3. **ConflictsPage** — vista lado a lado por `ConflictGroup`: valor de cada mod (y baseline si hay), RadioButtons para elegir ganador, "aplicar mod X a todos sus conflictos".
4. **MergePage** — resumen del plan (N cambios, M conflictos resueltos, pendientes bloquean), destino del pak, log de progreso, resultado con verificación.
5. **EasyMergePage** — flujo simplificado de merge.
6. **BuilderPage** — Stellar Souls Builder: cuestionario para compilar mods personalizados.
7. **CnsConverterPage** — conversor de outfits replacer ↔ CNS.
8. **CnsIdFixerPage** — escaneo y corrección de Container_Id duplicados.
9. **LivePage** — control en vivo mientras el juego corre: instalar/desinstalar el bridge, FOV, velocidad, salto.
10. **SettingsPage** — configuración: ruta del juego, tema, idioma, mappings, actualizaciones.
11. **SaveConverterPage** — conversor y reparador de partidas, con backup automático y crédito a lotress.

Componentes reutilizables en `qml/components` (FlatButton, FieldCombo, BulkTransformDialog, EditValueDialog, LanguageDialog, ThemedScrollBar, UpdateDialog). `Theme.qml` define las paletas Claro, Oscuro y OLED; `AppController.themeMode` persiste la selección en `QSettings` y la expone globalmente a QML.

`BuilderPage` envía opciones semánticas al compilador Python de `Builder/`.
Los cambios independientes son booleanos; las alternativas que modifican la
misma mecánica (perfil, economía Beta/Burst, región/densidad/dificultad) son
selecciones únicas. `table_compiler.py` parte de las tablas vanilla y copia
solamente los subconjuntos elegidos desde las bases autoritativas del mod. El
idioma de la guía de instalación se toma de `I18n.language`; no se configura
por separado dentro del Builder.

Los presets nombrados guardan el mismo objeto de respuestas completo en
`QSettings` (`builder/presets`), mientras el historial de builds sigue viviendo
en `%LOCALAPPDATA%\StellarSoulsBuilder\history`. Además se exportan e importan
como archivo `.stpreset`.

`CnsConverterPage` llama a `CnsConverterService` en un worker. La entrada se
extrae a una carpeta temporal, los `.uasset` se convierten mediante
`UAssetService`, y el resultado se empaqueta como IoStore UE4.26 mediante
`PakService`. `AppController` persiste hasta 100 entradas de historial en
`QSettings` (`cns/history`).

`CnsIdFixerPage` llama a `CnsIdFixerService` en un worker independiente. El
servicio valida el encabezado y la tabla de chunks de cada `.utoc`, agrupa
`Container_Id` repetidos y enumera los `Package_Id` de ExportBundle compartidos.

`LivePage` habla con `LiveService`, expuesto a QML como `Live` (context property
propia, sin pasar por `AppController`: la página no comparte estado con el
pipeline de merge). Ver §11.

---

## 6. Manejo de errores

- Cada paso externo (repak/UAssetGUI/cue4parse) reporta por-asset: un uasset que no convierte no aborta la ingesta; se lista como "no analizable" y se ofrece modo AssetReplaced.
- Merge escribe siempre a pak nuevo `zzz_StellarTool_Merged.pak` (prefijo `zzz` gana por orden alfabético); nunca modifica los mods de origen.
- Verificación post-merge: reabrir el pak generado, tojson, re-diff contra el MergePlan; discrepancia = error visible antes de instalar.
- Las herramientas externas pueden fallar **sin imprimir nada**: ver §7. Todo
  fallo de UAssetGUI deja un log en `%LOCALAPPDATA%/StellarTool/logs/` con
  comando, código de salida, salida de consola y el error real; en `fromjson`
  también se guarda el JSON de entrada al lado.
- El importador **encola** los mods: agregar varios llama `addMod` en ráfaga y
  descartar los que llegan mientras hay otra importación en curso perdía mods en
  silencio.
- Una tabla sin cambios aplicados **no se emite** al pak (pisaría al mod de origen con vanilla).
- Una tabla que falla la verificación **no cancela las demás**: se excluye, se registra en `merge_report.txt` y se avisa que el mod de origen debe permanecer habilitado para esa tabla.

---

## 7. Escribir uassets: lo que aprendimos a los golpes

Todo esto salió de depurar fallos reales reportados por usuarios. Son trampas
que no están documentadas en ningún lado y que cuestan horas si se re-descubren.

### UAssetGUI reporta sus errores por el PORTAPAPELES

No los imprime. Ante una excepción **copia el stack trace al clipboard y sale
con código 0 sin generar el archivo**. Por eso todo fallo se veía mudo
("UAssetGUI no produjo el uasset esperado", sin más). `UAssetService` recupera
ese texto (solo si parece un stack trace de UAssetAPI) y lo mete en el error y
en el log de `%LOCALAPPDATA%/StellarTool/logs/`.

Corolario para depurar a mano: si `fromjson` falla, **leé el portapapeles**.

### Los FName nuevos deben estar en el NameMap

UAssetAPI trata cualquier FName ausente del `NameMap` del asset como "dummy" y
al escribir tira `DummyFNameSerializationException`. Cualquier valor de texto o
enum nuevo lo dispara. `MergeEngine::registerFNames` y el `TableCompiler` del
Builder agregan los que falten de las filas tocadas antes de serializar.
Agregar nombres de más es inocuo: UAssetGUI recalcula
`NamesReferencedFromExportDataCount` al escribir (verificado).

### El float cero se serializa como el STRING `"+0"`

UAssetGUI escribe el cero flotante como `"+0"`/`"-0"`, no como `0`. La
normalización del diff lo pasa a `0`, así que al escribir un número encima los
tipos no coinciden y el cambio se rechazaba. Era **la causa dominante** de los
cambios "no escribibles": bloqueaba a todo mod que activa algo que en vanilla
vale cero (HP drain, bonuses). Ver `isFloatZeroString`.

### Pasar el usmap por RUTA, no por nombre

`fromjson <json> <uasset> <mappings>` acepta un nombre sólo si el `.usmap` está
en `%LOCALAPPDATA%/UAssetGUI/Mappings`. Pasar la ruta del archivo evita esa
dependencia por completo (`tojson` siempre lo hizo así).

### Una tabla sin cambios aplicados NO se emite

El pak mergeado carga con prioridad máxima (`zzz_`). Si una tabla termina con 0
cambios aplicados, empaquetarla significa escribir una copia de vanilla que
**pisa al mod de origen**: para el usuario la tabla "desaparece". `runMerge` la
deja fuera y lo dice en el resultado y en `merge_report.txt`.

Bitácora completa del problema, con los errores exactos, lo que se probó y lo
que quedó pendiente: [docs/ZEN_WRITE_BACK.md](docs/ZEN_WRITE_BACK.md).

### Reconstrucción de JSON clean (mods Zen)

`fillTemplate` reconstruye arrays/objetos desde la forma cruda vanilla y
`buildRowFromTemplate` hace lo mismo para filas nuevas. Antes de escribir una
fila reconstruida se convierten recursivamente los FName vacíos de `""` a
`"None"`; el nombre de la fila y sus FName nuevos se registran en `NameMap`.

Si un array vanilla está vacío, su `ArrayType` permite sintetizar wrappers para
tipos escalares. Un `StructProperty` vacío sigue requiriendo un layout de
plantilla y se saltea. `RowRemoved` clean se aplica directamente, salvo si un
mod pierde más del 25% de las filas vanilla: ese caso se considera una posible
exportación CUE4Parse incompleta y se bloquean sus borrados.

### Enums numerados (`FName_3`)

UE guarda `"Valor_3"` como el FName `"Valor"` con número, y UAssetGUI lo lee
expandido pero no sabe volver a escribirlo: el uasset no se genera y la tabla
entera queda fuera del merge. `MergeEngine::rewriteNumberedEnums` usa el usmap
para canonicalizar los valores conocidos como `ByteProperty` numérica (el índice
dentro del enum), incluso si el nombre ya está en el `NameMap`: UAssetGUI puede
releer un byte como texto (`0` → `Equal`) y ambas formas deben compararse igual.

### Trampa al depurar: instancias colgadas de UAssetGUI

Es una app GUI. Si queda una instancia abierta (por ejemplo al ejecutarla sin
argumentos), las corridas siguientes fallan **incluso con entradas válidas** y
los resultados no son reproducibles. Matá el proceso antes de cada prueba. Sus
`qWarning`/stderr tampoco se capturan desde el modo headless: para depurar hay
que escribir a archivo.

---

## 8. Build

CMake + Qt 6.4+ (Core, Quick, Concurrent, Widgets, Multimedia, Network), C++17, mismo esqueleto de `build.bat Release NOPAUSE` / `tests.bat` que LlamaCode. Tests con QtTest sobre `TableDiffEngine`, `MergeEngine`, `UpdateService`, `CnsConverter`, `CnsIdFixer`, `LiveService`, `HeadlessRunner` y `BuilderUi` usando fixtures JSON (sin depender de binarios del juego).

---

## 9. Modo headless (CLI)

### Catálogo de movesets precompilados

`MovesetService` inspecciona carpetas externas buscando tríos IoStore completos
(`.pak/.ucas/.utoc`) y clasifica sus nombres en familia (`fusion`, `scarlet`,
`raven`), tier y variante `aggro`. No interpreta el contenido binario ni copia
los archivos al repo. La instalación explícita copia el trío a `~mods`, rechaza
colisiones con archivos existentes y guarda un manifiesto en
`%LOCALAPPDATA%/StellarTool/moveset_install.json`; la desinstalación usa solo
ese manifiesto.

El contrato completo para agentes y automatizaciones, incluida la matriz por
sección, precondiciones, efectos y códigos de salida, está en
[docs/HEADLESS.md](docs/HEADLESS.md). La regla de arquitectura es que cada
operación que modifica datos o el juego tenga un camino CLI equivalente; solo
acciones de presentación (diálogos, abrir carpetas, tema e idioma visual) quedan
fuera del headless.

`HeadlessRunner` expone toda la funcionalidad de la app por línea de comandos:

```
StellarTool --headless analyze   --mod <ruta>... [--baseline <dir>]
StellarTool --headless merge     --mod <ruta>... --out <dir> [--prefer <mod>]
StellarTool --headless cns       --mod <ruta> --out <dir> [--name <nombre>]
StellarTool --headless replacer  --mod <ruta> --out <dir> --replace <outfit>
StellarTool --headless build     --answers <json|archivo> --out <dir>
StellarTool --headless baseline  [--game <dir>]
StellarTool --headless status
StellarTool --headless detect
StellarTool --headless uninstall [--paks] [--helper]
StellarTool --headless fixids    --mod <dir> [--apply]
StellarTool --headless presets
StellarTool --headless save-to-json   --input <sav> --out <json> [--indent <n>]
StellarTool --headless save-from-json --input <json> --out <sav>
StellarTool --headless fix-save       --input <sav>
StellarTool --headless reshade       --action <list|save|restore|rename|delete|import|export>
StellarTool --headless live          --action <status|install|uninstall|reset|set>
StellarTool --headless patch-validate --input <patch.toml|bundle.stpatch>
StellarTool --headless patch-preview  --input <patch.toml|bundle.stpatch> [--baseline <dir>]
StellarTool --headless patch-apply    --input <patch.toml|bundle.stpatch> --out <dir>
StellarTool --headless patch-export   --mod <ruta>... --out <dir>
```

Salida por stdout; exit code 0 = OK. `validate()` es testeable sin levantar la app entera.

---

## 10. Internacionalización

10 idiomas soportados (`i18n/*.json`): es, en, fr, it, de, ja, ko, pt_BR, ru, zh_Hans.
`Translator` carga el idioma activo y lo expone a toda la app (UI + headless + Builder).
La UI debe consumir las claves mediante `I18n.s`/`Translator`: los literales visibles
hardcodeados no cumplen el contrato, y cambiar el idioma debe actualizar los bindings
sin reiniciar. Las claves nuevas se agregan primero a `i18n/en.json` y se propagan a
los otros idiomas; mientras falte una traducción se usa el fallback inglés.
La migración histórica de pantallas todavía está pendiente en algunas páginas, por
lo que no se debe interpretar la existencia de `i18n/*.json` como cobertura completa.

---

## 11. Live: control en vivo (fase 1)

`LiveService` + `qml/pages/LivePage.qml` + el bridge Lua `assets/live/StellarToolLive`.
Todo el código es propio de Stellar Tool; no incorpora nada de otras suites de mods.

**Por qué reflection y no offsets.** El bridge resuelve las propiedades por
nombre sobre el `PlayerController` vivo (`PlayerCameraManager`,
`CharacterMovementComponent`) usando la reflection de UE4SS. No usa offsets ni
firmas de memoria, así que no queda atado a una versión puntual del juego: si
una property no existe en el build instalado, esa función queda apagada y el
resto sigue andando.

**Lo que el bridge NO hace**, por diseño: hooks, key binds, lectura de UObjects
en background, escritura del save, inventario, dinero, equipamiento,
progresión. Fase 1 es exclusivamente FOV / velocidad / salto.

**Protocolo** (texto clave=valor, publicación atómica `.tmp` + rename en ambos
lados, dentro de `…/ue4ss/Mods/StellarToolLive/`):

| Archivo | Lo escribe | Contenido |
|---|---|---|
| `live_request.txt` | Stellar Tool | `api`, `seq`, `fov` (0 = no tocar), `speed`, `jump` |
| `live_status.txt` | el bridge | `api`, `beat`, `ready`, `seq`, `fov_prop`, `*_base`, `*_live`, `message` |

El marcador `api=stellar-tool-live-v1` es obligatorio para parsear: un heartbeat
de otra herramienta en la misma carpeta se descarta como inválido.

**Vivo ≠ archivo presente.** `live_status.txt` queda en disco con los últimos
valores aunque el juego esté cerrado, así que `bridgeAlive` exige que el `beat`
cambie: sin beat nuevo por 2 s, desconectado.

**Reaplicación por tick.** El juego reescribe velocidad/salto/FOV solo (equipar,
cinemáticas, cambios de estado), por eso el bridge reaplica cada 250 ms en vez
de una sola vez por request. Al cambiar el pawn (cargar save, cambio de nivel)
recaptura los valores base antes de multiplicar: si no, el multiplicador se
aplicaría sobre un valor ya modificado y se acumularía.

**Límites** duplicados a propósito en C++ y en Lua (la UI no es la única
barrera): FOV 40–170° (>100 es experimental y la UI avisa), multiplicadores
0.1–10.

**Requisitos.** UE4SS lo instala el usuario por separado; Stellar Tool no lo
distribuye. Sin juego configurado o sin UE4SS, la página se muestra
deshabilitada con el motivo.

**Desinstalar** borra solo `Mods/StellarToolLive`, y únicamente si adentro está
nuestro `Scripts/main.lua`: si el usuario apuntó el juego a otra carpeta, no nos
llevamos puesto nada ajeno.
