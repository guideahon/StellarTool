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

## 7. Build

CMake + Qt 6.4+ (Core, Quick, Concurrent, Widgets), C++17, mismo esqueleto de `build.bat Release NOPAUSE` / `tests.bat` que LlamaCode. Tests con QtTest sobre `TableDiffEngine` y `MergeEngine` usando fixtures JSON (sin depender de binarios del juego).
