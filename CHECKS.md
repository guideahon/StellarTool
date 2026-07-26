# CHECKS.md — Verificaciones por fase

Checklist de aceptación. Marcar al completar; cada ítem debe ser demostrable (test automático o pasos manuales reproducibles).

## Fase 0 — Infraestructura
- [ ] `build.bat Release NOPAUSE` produce `build/Release/StellarTool.exe` en máquina limpia con Qt 6.4+.
- [ ] `tests.bat Release` corre y reporta (aunque la suite esté vacía).
- [ ] App abre con ventana, tema oscuro y navegación entre páginas placeholder.
- [ ] `tools/repak.exe` y `tools/UAssetGUI.exe` presentes; `tools/VERSIONS.md` con versiones y hashes.
- [ ] Logs escritos en `%LOCALAPPDATA%/StellarTool/logs/`.

## Fase 1 — Ingesta
- [ ] Cargar un `.pak` de mod real → lista de assets con rutas de juego correctas.
- [ ] Cargar `.zip` con pak adentro y carpeta suelta → mismo resultado.
- [ ] Tablas convertidas a JSON; asset corrupto/no convertible aparece como "no analizable" sin abortar el resto.
- [ ] Mod del post de Nexus: las 15 tablas listadas (SPLevelTable … AIGroupTable).
- [ ] Reordenar mods en la lista persiste y define prioridad por defecto.
- [ ] Round-trip smoke: unpack → pack sin cambios → pak resultante funcionalmente idéntico (mismo listado de archivos y tamaños de assets).

## Fase 2 — Baseline
- [ ] Con baseline: `ChangeItem` muestra `antes → después`.
- [ ] Sin baseline: app funcional, banner de modo degradado, diff solo entre mods.
- [ ] Importación de dump FModel según `docs/baseline.md` genera cache válida.
- [ ] Cache en `%LOCALAPPDATA%/StellarTool/baseline/`; botón regenerar la reconstruye.

## Fase 3 — Diff engine (tests automáticos, fixtures JSON)
- [ ] Fila modificada → un ChangeItem por propiedad hoja cambiada, `propertyPath` correcto.
- [ ] Fila añadida / eliminada detectadas (`RowAdded` / `RowRemoved`).
- [ ] Propiedades anidadas (struct dentro de struct) y arrays diffean bien.
- [ ] Floats: diferencia < epsilon relativo 1e-6 no genera ChangeItem.
- [ ] Dos mods, mismo path, valores distintos → un `ConflictGroup` con ambos candidatos.
- [ ] Dos mods, mismo path, mismo valor → sin conflicto, un solo check.
- [ ] Asset no tabular presente en dos mods → conflicto tipo AssetReplaced.
- [ ] Resúmenes humanos correctos, incl. % de cambio con baseline.

## Fase 4 — UI selección
- [ ] Check tri-state tabla/fila refleja hojas; toggle en cascada funciona.
- [ ] Filtro "solo conflictos" y búsqueda por texto funcionan.
- [ ] ConflictsPage: elegir ganador por grupo; acciones masivas por mod y por tabla; contador de pendientes llega a 0.
- [ ] Guardar y reabrir `.stproj` restaura mods, checks y resoluciones exactas.
- [ ] UI no se congela durante ingesta/análisis (operaciones async con progreso).

## Fase 5 — Merge
- [ ] Merge bloqueado con conflictos sin resolver, con lista de pendientes.
- [ ] Cambios seleccionados aplicados; NO seleccionados ausentes del pak final.
- [ ] Conflicto resuelto → valor del mod ganador en el pak final.
- [ ] `RowAdded`/`RowRemoved` aplicados correctamente.
- [ ] Assets no tabulares del mod ganador copiados al pak.
- [ ] Verificación automática post-merge (unpack + tojson + re-diff vs plan) verde y visible en UI.
- [ ] Paks de origen intactos; "desactivar" solo mueve a `~mods/disabled/` con confirmación.
- [ ] **Test manual in-game**: pak mergeado en `~mods`, juego carga, al menos un valor de cada mod de origen verificado en juego (documentar en `docs/manual-test.md`).

## Fase 6 — Release
- [ ] Zip de release con `windeployqt` + tools corre en máquina sin Qt instalado.
- [ ] README usuario final con instalación y flujo completo con capturas.
- [ ] Todos los mensajes de error tienen acción sugerida y acceso al log.

## Invariantes permanentes (verificar en cada PR que toque core)
- [ ] Ningún código parsea `.uasset` binario directamente.
- [ ] MergeEngine mantiene round-trip fiel (test de verificación pasa).
- [ ] Tests unitarios corren sin binarios externos ni archivos del juego.
- [ ] Ninguna operación escribe/borra archivos fuera de: dir temporal, cache LOCALAPPDATA, destino elegido por el usuario.
- [ ] Ningún fallo de herramienta externa queda mudo: siempre hay motivo visible + log.
- [ ] Ninguna tabla se emite al pak si no aplicó ningún cambio (pisaría al mod de origen con vanilla).

## Al tocar la escritura de uassets (MergeEngine / UAssetService)

Leer primero [ARCHITECTURE.md §7](ARCHITECTURE.md) — trampas ya pagadas.

- [ ] **Matar instancias colgadas de UAssetGUI antes de cada prueba.** Una GUI
      abierta hace fallar corridas válidas y vuelve los resultados no reproducibles.
- [ ] Si `fromjson` falla: **leer el portapapeles**, ahí está el stack trace real.
- [ ] Todo FName nuevo (texto/enum) queda registrado en el `NameMap` del asset.
- [ ] Escribir un número sobre el `"+0"` de UAssetGUI sigue funcionando (test
      `writesOverUAssetGuiFloatZero`).
- [ ] Medir contra un mod Zen real **antes y después**: comparar
      `applied`/`skipped` por tabla en `merge_report.txt`. Una mejora que no
      mueve esos números no justifica su costo.
