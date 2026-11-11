# Stellar Tool — Plan de implementación detallado

Referencia de arquitectura: [ARCHITECTURE.md](ARCHITECTURE.md). Cada fase termina con criterios de aceptación verificables (ver también CHECKS.md).

**Estado actual (v0.6.3)**: Fases 0-6 completas. Extensiones CNS Converter, CNS ID Fixer, Builder con selección granular, ajustes de mundo y presets compartibles implementadas. Modo headless completo.

---

## Fase 0 — Infraestructura y toolchain ✅
0.1. Esqueleto CMake (patrón LlamaCode: `CMakeLists.txt`, `src/`, `qml/`, `tests/`, `build.bat Release NOPAUSE`, `tests.bat`). Qt 6.4+: Core, Quick, Concurrent, Widgets, Multimedia, Network.
0.2. Carpeta `tools/` con binarios: `repak.exe`, `retoc.exe`, `UAssetGUI.exe`, `cue4parse.exe`, `StellarBlade.usmap`. Versiones en `tools/VERSIONS.md`.
0.3. `main.cpp` + `Main.qml` con tema Claro/Oscuro/OLED, ventana, navegación por páginas.
0.4. Logging a archivo (`%LOCALAPPDATA%/StellarTool/logs/`) con categoría por módulo (`qCDebug`).
- **Acepta**: app abre, build reproducible, tests.bat corre suite completa.

## Fase 1 — Ingesta de mods ✅
1.1. `PakService`: unpack/pack legacy (repak), to-legacy/to-zen (retoc), compat-global para revertir mods Zen, extracción de zips.
1.2. `UAssetService`: tojson/fromjson batch con cola y N procesos paralelos. Timeout 60 s por asset.
1.3. `Cue4Service`: lee contenedores Zen/IoStore que retoc no puede revertir, exportando DataTables a JSON.
1.4. `ModImporter`: pipeline completo zip/carpeta/pak/Zen → `ModPackage` con `ModAsset[]` clasificados (DataTable = tojson OK; Zen no convertible = CUE4Parse; resto = Other/Unreadable).
1.5. UI HomePage: drag&drop + FileDialog, lista de mods (nombre, #tablas, #assets otros, estado análisis), quitar mod, reordenar. Cola de importación para evitar perder mods.
- **Acepta**: cargar mods legacy y Zen; errores por asset visibles sin abortar.

## Fase 2 — Baseline vanilla ✅
2.1. `BaselineManager`: settings con ruta del juego; extracción con CUE4Parse bajo demanda (`ensureTables` exporta solo las tablas necesarias, no barre el juego entero).
2.2. Importación de dump JSON/uasset legacy desde carpeta del usuario.
2.3. Cache versionada en `%LOCALAPPDATA%/StellarTool/baseline/` como JSON por tabla. Botón "regenerar baseline". Detección de staleness (compara pakchunk0). Tablas ausentes cacheadas.
2.4. Modo degradado sin baseline: flags en UI, diffs mod-vs-mod solamente.
- **Acepta**: con baseline presente, cualquier tabla del mod muestra antes/después; sin baseline la app sigue funcional.

## Fase 3 — Diff engine (3 días) ✅ ⟵ núcleo
3.1. `TableDiffEngine::diffTable(base, mod) -> QList<ChangeItem>`:
   - Comparación por RowName: filas añadidas (`RowAdded`), eliminadas (`RowRemoved`), modificadas.
   - Dentro de fila: recursión por el árbol de propiedades UAssetAPI (match por `Name`; arrays por índice), emitiendo un `ChangeItem` por hoja distinta con `propertyPath` (`K:`, `N:`, `I:`).
   - Normalización numérica: floats comparados con epsilon relativo 1e-6 para no reportar ruido de serialización.
   - Normalización de documentos: `normalizeDataTableDoc` convierte UAssetAPI y CUE4Parse a forma canónica.
3.2. Resumen humano por `ChangeItem`: `"CharacterTable · EVE · MaxHP: 100 → 250 (+150%)"`; agregado por tabla.
3.3. Conflictos: `crossDiff(mods) -> QList<ConflictGroup>` — agrupar `ChangeItem` de distintos mods con misma key; si valores iguales, no es conflicto (cambio coincidente, un solo check). Duplicados exactos ocultos (`dup`).
3.4. Modelos QML: `ChangeListModel` (árbol Mod/Tabla/Fila/Prop con roles: summary, checked, conflictId, changeType) + proxy para filtros (solo conflictos, búsqueda, por tabla).
3.5. Tests QtTest con fixtures JSON sintéticas: cubren añadido/eliminado/modificado/anidado/array/float-epsilon/conflicto/coincidencia.
- **Acepta**: suite de diff verde; cargar 2 mods reales que tocan `CharacterTable` marca los conflictos correctos.

## Fase 4 — UI de selección y conflictos ✅
4.1. ChangesPage: TreeView, CheckBox tri-state a nivel tabla/fila, hoja con check individual. Check por defecto: todo seleccionado salvo conflictos sin resolver.
4.2. ConflictsPage: card por `ConflictGroup`: baseline (si hay) + valor de cada mod con radio; acciones masivas ("preferir Mod A en todo", "preferir Mod A solo en esta tabla"). Contador de pendientes.
4.3. Edición manual: permitir tipear un valor custom que reemplaza a ambos.
4.4. Edición masiva (×N, +, −, clamp… con regex de fila).
4.5. Import/export de TOML patches (estilo automod), operaciones declarativas,
     regex segura, bundles `.stpatch` y comandos headless de validate/preview/apply/export.
4.6. `ProjectStore`: guardar/cargar `.stproj` (rutas de mods, checks, resoluciones). Autosave.
- **Acepta**: flujo completo de selección sin merge; reabrir proyecto restaura estado exacto.

## Fase 5 — Merge y empaquetado ✅
5.1. `MergeEngine::buildPlan()` desde selecciones; valida: 0 conflictos sin resolver, si no bloquea con lista.
5.2. Aplicación: por cada tabla afectada, partir del JSON baseline (o del JSON del mod de mayor prioridad si no hay baseline) y aplicar cada `ChangeItem` seleccionado por `propertyPath`. `RowAdded`: insertar fila completa; `RowRemoved`: quitar.
5.3. Merge de mods Zen: números, texto, enums, arrays, objetos y filas nuevas/borradas. Arrays escalares con base vacía sintetizados. Guard del 25% para borrados masivos.
5.4. Assets `Other` seleccionados: copiar el archivo del mod ganador al árbol de salida.
5.5. Salida: `fromjson` → `retoc to-zen` (Zen/IoStore) o `repak pack` (legacy si no hay retoc). Verificación post-merge.
5.6. Verificación: unpack del pak generado, tojson, re-diff contra el plan; reporte "N/N cambios verificados". Tablas falladas excluidas sin cancelar el resto.
5.7. Tests de integración: fixtures → plan → merge → verificación en memoria (mock de servicios externos).
- **Acepta**: pak mergeado funcional; verificación automática verde.

## Fase 6 — Pulido ✅
6.1. Manejo robusto de errores (mensajes accionables, botón "abrir log").
6.2. Iconografía/tema (Claro, Oscuro, OLED), empty states, atajos.
6.3. `README.md` usuario final + documentación.
6.4. Empaquetado release: `windeployqt` + tools/ + zip.
6.5. Autoactualización desde GitHub Releases (`UpdateService`).
6.6. Internacionalización: 10 idiomas.

---

## Extensiones implementadas

### Plugin CPS complementario (MVP; falta validación en juego)
- Proyecto CMake x64 aislado, compilado con el SDK local de CPS y empaquetado como `.cps`.
- Menú en juego con controles configurables para vida/escudo, regeneración,
  velocidad de ataque, capacidad Beta/Burst/Tachy, munición y consumibles.
- Solo escribe números existentes de `CharacterTable / Player` tras validar la
  fila y el tipo. Restaura valores propios al desactivar o descargar; no pisa un
  valor que otro plugin haya cambiado después.
- Configuración persistente y textos en inglés/español.
- Queda pendiente portar otras funciones del Builder que sean seguras en
  runtime. Assets, outfits, creación de filas y cambios de `.pak` siguen siendo
  operaciones desktop/headless.

### Conversor CNS ✅
- Conversor nativo C++ replacer → CNS y CNS → replacer.
- Entrada `.zip`, `.pak`, `.utoc` o carpeta; selección de variante CNS y slot vanilla de destino.
- Reescritura de referencias por JSON UAssetAPI, empaquetado IoStore UE4.26 y descriptor `.dekcns.json`.
- Replacers conservan el esquema vanilla actual de la malla (exports y propiedades auxiliares, incluidos morph targets), usan por defecto el `PostProcessAnimBlueprint` vanilla, copian solo datos de fuente compatibles y reparan referencias de física. Validan el `.uasset` extraído del IoStore final y rechazan referencias usadas inválidas.
- Aislamiento de materiales/texturas compartidos al convertir a CNS; el descriptor agrega opciones de textura original y controles de rugosidad/metalicidad cuando están disponibles.
- Datos sincronizados con CNSRepacker 1.13 / Stellar Blade noviembre de 2026; conversión hacia Fusion Suit agrega la malla NoAcc y, si CUE4Parse puede leer los slots, los materiales que el jefe fuerza por índice.
- UI (`CnsConverterPage`) con flujo rápido ZIP → replacer, catálogo de outfits
  vanilla, salida Vortex-ready y opciones avanzadas; comandos headless `cns` /
  `replacer` cubren las mismas conversiones.
- Historial persistente (hasta 100 entradas) y ZIPs Vortex-ready.

### CNS ID Fixer ✅
- Sección propia para escanear recursivamente mods IoStore instalados.
- Detección y corrección de `Container_Id` duplicados con backup y verificación.
- Botón **Fix IDs and textures**: intenta convertir por separado los contenedores
  con `Package_Id` compartidos, aísla materiales/texturas y despliega los outfits
  compatibles en `CustomNanosuitSystem/Cosmetics`. Los paquetes incompatibles
  permanecen activos y se reportan. Solo conserva las conversiones si reducen
  las referencias compartidas; si queda algún conflicto, informa una reparación
  parcial y corrige también los `Container_Id` que sean compatibles.
- Los reportes del escaneo muestran rutas relativas completas para desambiguar
  contenedores con el mismo nombre.
- Verifica la integridad IoStore y reconstruye hashes dañados del `ContainerHeader`
  cuando son reparables, preservando assets/rutas de replacer, verificando los
  `Package_Id` y respaldando la familia original en `~mods/disabled/`.
- Compatible con `DirectoryIndex` de UE4.26; formatos perfect-hash se reportan y quedan intactos.

### Stellar Souls Builder ✅
- Cuestionario granular sin dropdowns: cambios independientes con check, valores superpuestos con radios.
- Compilación real por cambio (no presets prebuilt): daño Beta/Burst, drones, dash, EVE, enemigos, perfect dodge, Tachy, Blaster Cell, extras.
- Mini-boss con densidad independiente por región y configuración granular.
- Ajustes de mundo y progresión (`worldTweaks`): tienda, drops, EXP, mejoras EVE, pesca.
- Presets nombrados con export/import `.stpreset`.
- Helper CNS y helper vanilla (ALPHA) compilados dinámicamente.
- Instalación directa en el juego con desinstalación limpia.
- Detección de paks fantasma en `~mods`.
- Historial persistente con "usar de plantilla".
- CLI headless completo (`--headless build`).
- Catálogo granular de movesets Zen: análisis contra vanilla, cambios por
  propiedad/fila, grupos exclusivos, assets por bundle de variante y comando
  `--headless moveset-catalog`.

### Mappings versionados ✅
- Descarga de usmap por versión del juego desde el archivo de la comunidad.
- Detección automática de versión del juego (FileVersion del exe).
- Override de usmap en Settings sin reemplazar el bundled.
- Lectura de enums del usmap para diagnóstico y normalización; la escritura
  conserva `EnumPropertyData` cuando el schema Zen requiere su header original.

---

## Riesgos y mitigaciones
| Riesgo | Mitigación |
|---|---|
| UAssetGUI no round-tripea alguna tabla de SB | detectarlo en ingesta (fromjson+tojson de prueba); esa tabla cae a modo AssetReplaced todo-o-nada |
| Mods IoStore no convertibles por retoc | fallback a CUE4Parse (JSON clean); write-back limitado a escalares |
| Floats con ruido de serialización | epsilon relativo en diff (3.1) |
| UAssetGUI reporta errores por portapapeles | recuperado y logueado en `%LOCALAPPDATA%/StellarTool/logs/` |
| FName nuevos fuera del NameMap | `registerFNames` los agrega antes de serializar |
| Exportación CUE4Parse truncada → borrados masivos | guard del 25% de filas faltantes |

## Estimación total
~14-16 días de trabajo efectivo para v1.0. **Completado en v0.6.3.**
