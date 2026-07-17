# Stellar Tool — Plan de implementación detallado

Referencia de arquitectura: [ARCHITECTURE.md](ARCHITECTURE.md). Cada fase termina con criterios de aceptación verificables (ver también CHECKS.md).

## Fase 0 — Infraestructura y toolchain (1 día)
0.1. Crear esqueleto CMake (copiar patrón de LlamaCode: `CMakeLists.txt`, `src/`, `qml/`, `tests/`, `build.bat Release NOPAUSE`, `tests.bat`). Qt 6.4+: Core, Quick, Concurrent, Widgets, Svg.
0.2. Carpeta `tools/` con binarios: `repak.exe` (github.com/trumank/repak, release win64) y `UAssetGUI.exe` (github.com/atenfyr/UAssetGUI). Documentar versiones exactas en `tools/VERSIONS.md`.
0.3. `main.cpp` + `Main.qml` mínimo con tema oscuro, ventana, navegación por páginas (StackLayout + NavBar simple).
0.4. Logging a archivo (`%LOCALAPPDATA%/StellarTool/logs/`) con categoría por módulo (`qCDebug`).
- **Acepta**: app abre, build reproducible, tests.bat corre suite vacía.

## Fase 1 — Ingesta de mods (2-3 días)
1.1. `PakService`:
   - `unpack(pakPath, outDir)` → QProcess repak, async (QtConcurrent), progreso por stdout.
   - `pack(dir, outPak)` con `--version V11` y mount point correcto (`../../../` estilo UE; validar contra un pak de mod real).
   - Manejo zip: extraer con miniz (vendorizado) o `tar -xf` de Windows; carpeta: copiar/enlazar tal cual. Detectar dentro del zip: uno o varios `.pak`.
1.2. `UAssetService`:
   - `toJson(uassetPath) -> jsonPath` con `UAssetGUI tojson <in> <out> VER_UE4_26`.
   - `fromJson(jsonPath, outUasset)`.
   - Batch con cola y N procesos paralelos (config, default 4). Timeout 60 s por asset.
1.3. `ModImporter`: pipeline completo zip/carpeta/pak → `ModPackage` con `ModAsset[]` clasificados (DataTable = tojson OK y `$type` DataTableExport presente; resto = Other).
1.4. UI HomePage: drag&drop + FileDialog, lista de mods (nombre, #tablas, #assets otros, estado análisis), quitar mod, reordenar (define prioridad por defecto en conflictos).
- **Acepta**: cargar el mod del post de Nexus (15 tablas) y verlo listado con sus tablas; cargar un segundo mod cualquiera; errores por asset visibles sin abortar.

## Fase 2 — Baseline vanilla (2 días)
2.1. `BaselineManager`: settings con ruta del juego (`steamapps/common/StellarBlade`); intento de extracción directa de las tablas conocidas de los paks del juego con repak (probar; los paks principales pueden estar cifrados/IoStore).
2.2. Si extracción directa falla: flujo de importación — el usuario exporta las tablas con FModel (guía en `docs/baseline.md` con clave AES referenciada, no incluida) y la tool ingiere ese dump JSON/uasset.
2.3. Cache versionada en `%LOCALAPPDATA%/StellarTool/baseline/<gameBuild>/` como JSON por tabla. Botón "regenerar baseline".
2.4. Modo degradado sin baseline: flags en UI, diffs mod-vs-mod solamente.
- **Acepta**: con baseline presente, cualquier tabla del mod muestra antes/después; sin baseline la app sigue funcional.

## Fase 3 — Diff engine (3 días) ⟵ núcleo
3.1. `TableDiffEngine::diffTable(base, mod) -> QList<ChangeItem>`:
   - Comparación por RowName: filas añadidas (`RowAdded`), eliminadas (`RowRemoved`), modificadas.
   - Dentro de fila: recursión por el árbol de propiedades UAssetAPI (match por `Name`; arrays por índice), emitiendo un `ChangeItem` por hoja distinta con `propertyPath` (`Struct.MaxHP`, `Skills[3].Cost`).
   - Normalización numérica: floats comparados con epsilon relativo 1e-6 para no reportar ruido de serialización.
3.2. Resumen humano por `ChangeItem`: `"CharacterTable · EVE · MaxHP: 100 → 250 (+150%)"`; agregado por tabla ("CharacterTable: 12 cambios en 4 filas").
3.3. Conflictos: `crossDiff(mods) -> QList<ConflictGroup>` — agrupar `ChangeItem` de distintos mods con misma key; si valores iguales, no es conflicto (cambio coincidente, un solo check). Assets `Other` con misma ruta = `ConflictGroup` de tipo asset.
3.4. Modelos QML: `ChangeListModel` (árbol Mod/Tabla/Fila/Prop con roles: summary, checked, conflictId, changeType) + proxy para filtros (solo conflictos, búsqueda, por tabla).
3.5. Tests QtTest con fixtures JSON sintéticas (tabla base, mod A, mod B): cubren añadido/eliminado/modificado/anidado/array/float-epsilon/conflicto/coincidencia.
- **Acepta**: suite de diff verde; cargar 2 mods reales que tocan `CharacterTable` marca los conflictos correctos.

## Fase 4 — UI de selección y conflictos (3 días)
4.1. ChangesPage: TreeView (o ListView plana con secciones colapsables), CheckBox tri-state a nivel tabla/fila, hoja con check individual. Check por defecto: todo seleccionado salvo conflictos sin resolver.
4.2. ConflictsPage: card por `ConflictGroup`: baseline (si hay) + valor de cada mod con radio; acciones masivas ("preferir Mod A en todo", "preferir Mod A solo en esta tabla"). Contador de pendientes.
4.3. Edición manual opcional (v1.1, no bloqueante): permitir tipear un valor custom que reemplaza a ambos.
4.4. `ProjectStore`: guardar/cargar `.stproj` (rutas de mods, checks, resoluciones). Autosave.
- **Acepta**: flujo completo de selección sin merge; reabrir proyecto restaura estado exacto.

## Fase 5 — Merge y empaquetado (3 días)
5.1. `MergeEngine::buildPlan()` desde selecciones; valida: 0 conflictos sin resolver, si no bloquea con lista.
5.2. Aplicación: por cada tabla afectada, partir del JSON baseline (o del JSON del mod de mayor prioridad si no hay baseline) y aplicar cada `ChangeItem` seleccionado por `propertyPath` (set recursivo). `RowAdded`: insertar fila completa; `RowRemoved`: quitar.
5.3. Assets `Other` seleccionados: copiar el archivo del mod ganador al árbol de salida.
5.4. `fromjson` de cada tabla → árbol de salida con rutas de juego correctas → `repak pack` a `zzz_StellarTool_Merged.pak` en destino elegido (default `~mods`).
5.5. Verificación: unpack del pak generado, tojson, re-diff contra el plan; reporte "N/N cambios verificados". Opción "instalar en ~mods y desactivar mods de origen" (mover a `~mods/disabled/` los paks originales — con confirmación; nunca borrar).
5.6. Tests de integración: fixtures → plan → merge → verificación en memoria (mock de servicios externos) + un test manual documentado con paks reales.
- **Acepta**: pak mergeado carga en el juego con los valores elegidos (test manual); verificación automática verde.

## Fase 6 — Pulido (2 días)
6.1. Manejo robusto de errores (mensajes accionables, botón "abrir log").
6.2. Iconografía/tema, empty states, atajos.
6.3. `README.md` usuario final + `docs/baseline.md`.
6.4. Empaquetado release: `windeployqt` + tools/ + zip.

## Riesgos y mitigaciones
| Riesgo | Mitigación |
|---|---|
| UAssetGUI no round-tripea alguna tabla de SB | detectarlo en ingesta (fromjson+tojson de prueba); esa tabla cae a modo AssetReplaced todo-o-nada |
| Paks del juego cifrados → sin baseline automática | flujo FModel documentado + modo degradado (Fase 2.2/2.4) |
| Mods IoStore (.ucas/.utoc) | fuera de alcance v1; detectar y avisar claramente |
| Mount point / versión de pak incorrecta al pack | validar contra pak de mod conocido que ya funciona in-game (Fase 1.1) |
| Floats con ruido de serialización | epsilon relativo en diff (3.1) |

## Estimación total
~14-16 días de trabajo efectivo para v1.0.
