# Stellar Tool

Herramienta de escritorio (Qt 6/QML, Windows) para analizar y **mergear mods de Stellar Blade**.

- Carga mods como `.pak`, `.zip` o carpeta.
- Lista cada cambio de DataTable (fila/propiedad) con checkbox.
- Detecta conflictos entre mods y permite elegir con qué valor quedarse.
- Genera un único `zzz_StellarTool_Merged.pak` verificado, listo para `~mods`.

Docs: [ARCHITECTURE.md](ARCHITECTURE.md) · [PLAN.md](PLAN.md) · [AGENTS.md](AGENTS.md) · [CHECKS.md](CHECKS.md)

## Instalación (desde código)

Requisitos: Windows 10/11, [Qt 6.4+](https://www.qt.io/download-open-source) (MSVC x64; el build script asume `C:\Qt\6.8.3\msvc2022_64`), Visual Studio Build Tools 2019/2022 y CMake 3.21+.

```bat
git clone https://github.com/guideahon/StellarTool.git
cd StellarTool
setup.bat                   :: descarga a tools\: repak, retoc, UAssetGUI y StellarBlade.usmap
build.bat Release NOPAUSE   :: genera build\Release\StellarTool.exe (con Qt deployado)
tests.bat Release           :: corre la suite de unit tests (diff/merge)
```

Si tu Qt está en otra ruta, editá `QT_DIR` al inicio de `build.bat` y `tests.bat`.
Los binarios externos no se versionan; `setup.bat` los baja de sus releases oficiales
(ver [tools/VERSIONS.md](tools/VERSIONS.md)).

## Uso

1. **Mods**: arrastrá tus mods (.pak/.zip/carpeta). El orden define prioridad (el primero gana).
2. Opcional: **Importar baseline** — carpeta con JSONs de tablas vanilla (dump de UAssetGUI/FModel) para ver "antes → después". Sin baseline la tool funciona igual, comparando solo entre mods.
3. **Analizar cambios** → pestaña **Cambios**: check por cambio, filtro por texto o solo conflictos, todo/nada por tabla.
4. **Conflictos**: elegí el ganador por cada uno (o "preferir mod X en todo").
5. **Merge**: elegí destino (idealmente `steamapps\common\StellarBlade\SB\Content\Paks\~mods`) y generá el pak. La tool verifica el resultado reconvirtiendo cada tabla. Los mods originales no se tocan: acordate de sacarlos de `~mods` para que no pisen el merge (el prefijo `zzz` le da prioridad de carga igualmente).

Los proyectos (mods + selecciones + resoluciones) se guardan como `.stproj`.

## Modo headless (CLI)

```bat
StellarTool --headless analyze --mod "<pak/zip/carpeta>" --mod "<otro>" [--baseline <dir>]
StellarTool --headless merge   --mod "<mod prioritario>" --mod "<otro>" --out <dir> ^
                               [--baseline <dir>] [--prefer <nombreMod>]
```

- `analyze` lista todos los cambios y marca conflictos; `merge` además genera el pak
  y un **`zzz_StellarTool_Merged.zip` instalable** (`Paks\` + readme) listo para
  Vortex u otro mod manager (`--no-zip` para omitirlo; en la UI es un checkbox).
- Conflictos: gana el primer `--mod` (prioridad por orden), salvo `--prefer`.
- `--baseline` acepta carpetas con JSONs de UAssetGUI o `.uasset` legacy (los convierte).
- Exit code 0 = OK; salida apta para scripts/CI.

## Formatos de Stellar Blade

- **Salida**: con `retoc.exe` presente se genera contenedor **Zen/IoStore**
  (`zzz_StellarTool_Merged_P.utoc/.ucas/.pak`, verificado con `retoc verify`) — el
  formato nativo del juego. Sin retoc, pak legacy V11.
- **Entrada**: paks legacy, zips o carpetas con `.uasset`. Los mods ya empaquetados
  como Zen (`.pak` cáscara + `.ucas/.utoc`) no se pueden desempaquetar (limitación
  de retoc); usá la carpeta fuente legacy del mod.
- **Baseline vanilla**: extraíble del juego con
  `retoc to-legacy -f "<Tabla>" --version UE4_26 "<StellarBlade>\SB\Content\Paks" <out>`
  e importable desde la app o con `--baseline`.
  **Si tenés mods instalados en `~mods`**: extraé desde una carpeta staging que
  contenga solo los contenedores raíz del juego (`global.*` + `pakchunk*`) para
  garantizar baseline vanilla pura. En la práctica retoc resuelve desde los
  pakchunks aunque `~mods` esté presente (verificado por hash), pero el staging
  elimina la duda:
  ```bat
  mkdir stage & copy "<Paks>\global.*" stage & copy "<Paks>\pakchunk*" stage
  retoc to-legacy -f "<Tabla>" --version UE4_26 stage <out>
  ```
