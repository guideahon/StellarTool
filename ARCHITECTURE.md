# Stellar Tool — Arquitectura

Herramienta de escritorio (Windows) para inspeccionar y **mergear mods de Stellar Blade**, con UI en **Qt 6 / QML** (mismo stack y convenciones que LlamaCode: core en C++ bajo `src/core`, UI en `qml/pages` + `qml/components`).

## 1. Contexto del dominio

- Los mods de Stellar Blade son archivos **`.pak` de Unreal Engine 4.26** (a veces con `.ucas/.utoc` si usan IoStore; los mods de gameplay típicos de `~mods` son `.pak` "legacy", sin firma y sin cifrado).
- Los cambios de gameplay viven en **DataTables**: assets `.uasset` + `.uexp` (ej. `SB/Content/Local/Data/CharacterTable.uasset`). Cada DataTable es un mapa `RowName -> Struct` con propiedades tipadas (float, int, bool, FName, structs anidados, arrays).
- **Conflicto**: dos mods incluyen el mismo asset (misma ruta dentro del pak). El motor carga uno solo (por orden alfabético del pak). El merge real requiere fusionar a nivel **fila/propiedad** y reempaquetar un único pak.

## 2. Pipeline de datos

```
mod.zip / carpeta / mod.pak
        │  (1) Ingesta
        ▼
   Extracción .pak  ──────────  repak.exe (CLI externa, QProcess)
        │  (2) árbol de assets extraídos
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
   UAssetGUI.exe fromjson  →  repak.exe pack  →  zzz_StellarTool_Merged.pak
```

### Herramientas externas (bundled en `tools/`)
| Tool | Uso | Invocación |
|---|---|---|
| `repak.exe` | unpack/pack de `.pak` (UE4.26, version v11) | `repak unpack`, `repak pack --version V11` |
| `UAssetGUI.exe` | `.uasset` ↔ JSON (motor UAssetAPI) | `UAssetGUI tojson <uasset> <json> VER_UE4_26`, `fromjson` |

Ambas se invocan vía `QProcess` con timeout, captura de stdout/stderr y verificación de exit code. Nunca se parsea binario uasset a mano.

### Baseline (vanilla)
Para poder decir "este mod cambió X de 100 a 250" hace falta la tabla **original**. Estrategias, en orden:
1. **Cache local de baseline**: primera vez, la tool extrae las tablas vanilla desde los paks del juego (ruta Steam configurable). Si los paks del juego están cifrados/IoStore y no se pueden leer, se cae a 2.
2. **Baseline importada**: el usuario apunta a un dump JSON de tablas vanilla (se documenta cómo generarlo con FModel + clave AES).
3. **Sin baseline (degradado)**: la tool muestra los valores del mod sin "antes/después", y el diff se hace solo **entre mods** (suficiente para detectar y resolver conflictos).

El diff mod-vs-mod **nunca** requiere baseline: conflicto = misma tabla + misma fila + misma propiedad con valores distintos.

## 3. Modelo de datos (C++ core)

```
ModPackage            // un mod cargado (zip/carpeta/pak)
 ├─ id, name, sourcePath, loadOrder
 └─ assets: QList<ModAsset>
ModAsset              // un uasset dentro del mod
 ├─ gamePath          // "SB/Content/Local/Data/CharacterTable.uasset"
 ├─ kind              // DataTable | Other (binario no tabular)
 └─ table: DataTableDoc?
DataTableDoc          // JSON parseado (QJsonDocument retenido para round-trip)
 └─ rows: QMap<QString /*RowName*/, QJsonObject>
ChangeItem            // unidad seleccionable ("check")
 ├─ modId, tablePath, rowName, propertyPath   // propertyPath con notación a.b[2].c
 ├─ baseValue?  (si hay baseline)
 ├─ newValue
 ├─ changeType       // Modified | RowAdded | RowRemoved | AssetReplaced(no-tabular)
 ├─ selected: bool
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
- Assets no tabulares (meshes, texturas si el mod los trae) se tratan como **AssetReplaced**: check todo-o-nada, conflicto = elegir un mod entero para ese asset.

## 4. Módulos C++ (`src/core`)

| Módulo | Responsabilidad |
|---|---|
| `PakService` | wrapper QProcess de repak: unpack a dir temporal por mod, pack final. Detección de zip/carpeta/pak (zip se extrae con `KZip`/miniz o QProcess + tar). |
| `UAssetService` | wrapper QProcess de UAssetGUI: tojson/fromjson, con detección de versión y reporte de fallos por asset. |
| `BaselineManager` | cache de tablas vanilla en `%LOCALAPPDATA%/StellarTool/baseline/`, extracción desde el juego o importación. |
| `ModImporter` | orquesta ingesta: extrae, convierte, clasifica assets, produce `ModPackage`. Corre en `QtConcurrent` con progreso. |
| `TableDiffEngine` | diff JSON recursivo: mod vs baseline → `ChangeItem[]`; cruza mods → `ConflictGroup[]`. |
| `MergeEngine` | aplica `MergePlan` sobre JSON base, invoca fromjson + pack, valida resultado (re-tojson y re-diff de verificación). |
| `ProjectStore` | guarda/carga sesión (`.stproj` JSON): mods cargados, selecciones, resoluciones. |
| `AppController` | fachada QObject expuesta a QML (patrón LlamaCode). Modelos: `ModListModel`, `ChangeListModel` (por tabla, con roles para check/conflicto), `ConflictModel`. |

## 5. UI (QML)

Páginas (`qml/pages`):
1. **HomePage** — drop zone (zip/carpeta/pak), lista de mods cargados con orden, botón "Analizar".
2. **ChangesPage** — árbol: Mod → Tabla → Fila → Propiedad. Cada hoja con CheckBox, texto resumen ("`CharacterTable / EVE / MaxHP: 100 → 250`"), badge de conflicto. Filtros: solo conflictos / por tabla / búsqueda.
3. **ConflictsPage** — vista lado a lado por `ConflictGroup`: valor de cada mod (y baseline si hay), RadioButtons para elegir ganador, "aplicar mod X a todos sus conflictos".
4. **MergePage** — resumen del plan (N cambios, M conflictos resueltos, pendientes bloquean), destino del pak, log de progreso, resultado con verificación.

Componentes reutilizables en `qml/components` (Card, SectionHeader, tema oscuro estilo LlamaCode). Estado global vía `AppController` (contextProperty o singleton QML).

## 6. Manejo de errores

- Cada paso externo (repak/UAssetGUI) reporta por-asset: un uasset que no convierte no aborta la ingesta; se lista como "no analizable" y se ofrece modo AssetReplaced.
- Merge escribe siempre a pak nuevo `zzz_StellarTool_Merged.pak` (prefijo `zzz` gana por orden alfabético); nunca modifica los mods de origen.
- Verificación post-merge: reabrir el pak generado, tojson, re-diff contra el MergePlan; discrepancia = error visible antes de instalar.
- Las herramientas externas pueden fallar **sin imprimir nada**: ver §7. Todo
  fallo de UAssetGUI deja un log en `%LOCALAPPDATA%/StellarTool/logs/` con
  comando, código de salida, salida de consola y el error real; en `fromjson`
  también se guarda el JSON de entrada al lado. Es lo primero que hay que pedir
  en un reporte de usuario.
- El importador **encola** los mods: agregar varios llama `addMod` en ráfaga y
  descartar los que llegan mientras hay otra importación en curso perdía mods en
  silencio.

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
enum nuevo lo dispara. `MergeEngine::registerFNames` agrega los que falten de
las filas tocadas. Agregar nombres de más es inocuo: UAssetGUI recalcula
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

### Lo que no round-tripea (limitación abierta)

Para mods **Zen** (leídos con CUE4Parse, `clean=true`) se escriben números,
strings y enums. Siguen sin soportarse:

- **Filas enteras nuevas o quitadas.**
- **Arrays y referencias a objetos.**

Se intentó reconstruir la forma cruda usando otra fila/propiedad de la misma
tabla como plantilla (`fillTemplate`, `buildRowFromTemplate`,
`addPropFromTemplate` — el código quedó porque `addPropFromTemplate` sí sirve
para propiedades que vanilla omite). La reconstrucción **se aplica bien y
produce JSON válido**, pero el uasset resultante no sobrevive el verify
round-trip. La causa está dentro del serializador de UAssetAPI y no se
identificó. Medido sobre un mod Zen real: habilitarlo daba **cero** cambios
aplicados de más y costaba un pase completo de `fromjson`+verify sobre tablas de
cientos de MB, así que está deshabilitado a propósito.

Dato adicional sin explicar: el asset vanilla referencia ~325 FName que **no
están** en su propio NameMap y aun así se escribe bien, o sea que UAssetAPI
resuelve nombres por otra vía además del NameMap local.

### Trampa al depurar: instancias colgadas de UAssetGUI

Es una app GUI. Si queda una instancia abierta (por ejemplo al ejecutarla sin
argumentos), las corridas siguientes fallan **incluso con entradas válidas** y
los resultados no son reproducibles. Matá el proceso antes de cada prueba. Sus
`qWarning`/stderr tampoco se capturan desde el modo headless: para depurar hay
que escribir a archivo.

## 8. Build

CMake + Qt 6.4+ (Core, Quick, Concurrent, Widgets), C++17, mismo esqueleto de `build.bat Release NOPAUSE` / `tests.bat` que LlamaCode. Tests con QtTest sobre `TableDiffEngine` y `MergeEngine` usando fixtures JSON (sin depender de binarios del juego).
