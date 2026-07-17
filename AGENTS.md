# AGENTS.md — Stellar Tool

Instrucciones para agentes que trabajen en este repo.

## Contexto
Herramienta Qt6/QML (Windows) para analizar y mergear mods de Stellar Blade (.pak UE4.26 con DataTables .uasset). Leer [ARCHITECTURE.md](ARCHITECTURE.md) antes de tocar el core; [PLAN.md](PLAN.md) define fases y alcance. Proyecto hermano de referencia para convenciones Qt/QML: `C:\Users\cristian\Documents\LlamaCode` (no copiar código a ciegas; sí patrones: AppController como fachada QObject, modelos C++ expuestos a QML, tema en ThemeProvider).

## Reglas de trabajo
- No leer todo el proyecto por defecto: buscar con `rg`, abrir solo lo relevante, respetar límites de módulo (`src/core/*Service`, `src/core/*Engine`, `qml/pages`, `qml/components`).
- Todo bug arreglado incluye test de regresión cuando sea viable. Toda feature nueva cubre camino feliz + bordes principales.
- Compilar para validar: `build.bat Release NOPAUSE` (nunca sin NOPAUSE, el `pause` bloquea automatización). Verificar que se actualizó `build/Release/StellarTool.exe`.
- Correr `tests.bat Release` antes de terminar si se tocó C++/QML/core. Si no se puede, dejar el motivo concreto.
- Cambios acotados; sin refactors amplios innecesarios. No revertir cambios ajenos: si el working tree está sucio, trabajar alrededor y reportar.
- Si el cambio altera comportamiento, build, arquitectura o flujo, actualizar ARCHITECTURE.md / PLAN.md / README.

## Reglas específicas del dominio
- **Nunca parsear binario `.uasset` a mano.** Toda conversión pasa por `UAssetService` (UAssetGUI CLI). Todo pak pasa por `PakService` (repak CLI). Binarios en `tools/`, versiones fijadas en `tools/VERSIONS.md`; no actualizar versiones sin correr la verificación de round-trip.
- **Round-trip fiel**: el merge aplica cambios sobre el JSON original (baseline o mod prioritario), jamás regenera estructura JSON desde el modelo interno. Cualquier cambio a `MergeEngine` debe mantener esta invariante y pasar el test de verificación (re-unpack + re-diff).
- **No destructivo**: la tool nunca modifica ni borra los paks de origen ni archivos del juego. Salida siempre a pak nuevo (`zzz_StellarTool_Merged.pak`). "Desactivar" mods = mover a `~mods/disabled/`, siempre con confirmación del usuario.
- Comparación de floats en diff: epsilon relativo 1e-6. No "arreglar" diffs ruidosos cambiando la serialización.
- Los tests unitarios de diff/merge usan fixtures JSON en `tests/fixtures/`; no deben requerir repak, UAssetGUI ni archivos del juego. Los tests que sí los requieren van marcados como integración y se saltean si faltan los binarios.
- Rutas de assets dentro del pak (`gamePath`) se comparan case-insensitive (Windows/UE).
- No incluir en el repo claves AES, assets del juego ni contenido extraído de los paks oficiales. La baseline vive en `%LOCALAPPDATA%/StellarTool/baseline/` del usuario.

## UI
- QML: páginas en `qml/pages`, componentes reutilizables en `qml/components`. Lógica en C++; QML solo presentación y bindings. Nada de lógica de merge/diff en JS.
- Estados vacíos, progreso y errores siempre visibles: toda operación larga (unpack, tojson batch, merge) corre async con progreso cancelable; la UI nunca se congela.
- Textos de UI en español neutro.
