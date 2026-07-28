# Stellar Tool

Herramienta de escritorio (Qt 6/QML, Windows) para analizar y **mergear mods de Stellar Blade**.

La interfaz incluye temas **Claro**, **Oscuro** y **OLED** (negro puro), seleccionables desde Settings. La elección queda guardada entre sesiones.

- Carga mods como `.pak`, `.zip` o carpeta.
- Lista cada cambio de DataTable (fila/propiedad) con checkbox.
- Detecta conflictos entre mods y permite elegir con qué valor quedarse.
- Genera un único `zzz_StellarTool_Merged.pak` verificado, listo para `~mods`.

Docs: [ARCHITECTURE.md](ARCHITECTURE.md) · [PLAN.md](PLAN.md) · [AGENTS.md](AGENTS.md) · [CHECKS.md](CHECKS.md)
Estado y pendientes: [docs/PENDIENTES.md](docs/PENDIENTES.md) · [docs/ZEN_WRITE_BACK.md](docs/ZEN_WRITE_BACK.md)

## Instalación (usuario final)

Descargá el `StellarTool-<version>.zip` de la pestaña **Releases** del repo (o de
Nexus Mods), extraelo donde quieras y ejecutá `StellarTool.exe`. Es autocontenido:
incluye el runtime de Qt y los binarios de `tools\` (repak, retoc, UAssetGUI y el
mapping de Stellar Blade). No instala nada ni toca archivos del juego.

### Actualizaciones

Al arrancar consulta la release *latest* de GitHub. Si hay una más nueva ofrece
tres opciones: **actualizar ahora** (descarga el zip, lo extrae y relanza el exe
nuevo), **más tarde** (vuelve a preguntar en el próximo arranque) o **saltear
esta versión** (no vuelve a ofrecer ese tag). El chequeo automático se apaga en
Settings → Actualizaciones, donde además hay un botón para buscar a mano.

Requisitos del lado del repo: la release tiene que estar **publicada** (no draft
ni prerelease, si no la API `releases/latest` devuelve 404) y traer como adjunto
el `StellarTool-<version>.zip` que genera `package.bat`.

## Empaquetar un release (mantenedores)

```bat
package.bat NOPAUSE   :: setup + build Release + genera dist\StellarTool-<version>.zip
```

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

### Stellar Souls Builder

La página **Build Stellar Souls** compila un Stellar Souls personalizado por cambio:
daño Beta/Burst, drones, dash, ataques de EVE, daño/vulnerabilidad de enemigos,
perfect dodge, duración de Tachy, economía de gauges, Blaster Cell, outfit,
mini-bosses y extras independientes. Los cambios que pisan la misma mecánica
se presentan como radios visibles de selección exclusiva; los combinables usan
checks. Al activar un cambio incompatible con otro que ya está marcado, la app
explica el conflicto y pregunta explícitamente cuál de los dos conservar.
**Full**, **First Run** y **NG+** son solamente presets que preseleccionan
controles: no ocultan ni bloquean cambios. Daño, gauges y otros valores graduables
ofrecen niveles visibles; los mini-bosses permiten densidad independiente por
región y configuración separada de vida, ataque, tamaño, escudo, drops, XP,
persistencia, tipo Boss e inmunidad a ejecución. El cuestionario no usa menús
desplegables.
Los extras permiten recrear de forma granular Base Attribute Enhancement:
HP/escudo y reducción base, regeneración de escudo 160/20, capacidad
Beta/Burst 1500/2000, HP pasivo 20/s, pesca 50, munición x100, recuperación
sostenida por acciones perfectas, dash de 4 s, escaneo 5/10 s y rotación
GunGorgon. Los extras cuantitativos de atributos, munición, consumibles,
regeneración, gauges, pesca y velocidad de ataque incluyen slider y campo
numérico sincronizados; **Vanilla** desactiva el cambio y restaura sus valores
originales.
La curación base del Tumbler se activa por separado y permite elegir cualquier
nivel de 10% a 100% en pasos de 10%; los proyectos anteriores conservan 60%.
Los grupos largos se distribuyen en varias filas para mantenerse dentro de la
tarjeta también en ventanas angostas.
Las configuraciones completas también se pueden guardar como presets con nombre,
cargar y eliminar independientemente del historial de compilaciones.
El perfil nativo de Stellar Souls modifica sólo enemigos normales. Los
mini-bosses se agregan de forma independiente y el extra **Bosses más duros**
ofrece presets Main e Insane limitados a Hardcore desde
`DifficultyStatGroupTable`; no modifica enemigos normales y excluye Maelstrom.
El pak combinado de mini-bosses aplica la misma selección granular sobre
`CharacterTable` y `SkillTable`: activar mini-bosses no reemplaza silenciosamente
la economía Beta/Burst ni el resto del perfil por un preset fijo. El
`build_manifest.json` registra los transforms aplicados por tabla.
**Usar de plantilla** vuelve a leer la configuración completa por ID desde el
historial persistente, por lo que también restaura extras BETA, densidades y
atributos de mini-bosses después de cerrar y abrir la aplicación.
**Sin daño por caída** neutraliza las cinco variantes vanilla de caída
(`ImmediateDeath`, warp y daño porcentual), incluida la variante que conserva
la secuencia cinemática.
El helper CNS aleatorio espera el flanco confirmado de escudo completo
(detach de BS_102/salida de SkinSuit). Antes de elegir o escribir un outfit
vuelve a leer la malla actual y cancela si EVE todavía usa SkinSuit, Tachy o
Fusion; nunca restaura al aparecer BS_102 durante la rotura del escudo.
La guía incluida en el ZIP usa automáticamente el idioma actual de la aplicación.

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

- **Entrada**: paks legacy, zips o carpetas con `.uasset`, **y mods Zen/IoStore**
  (`.pak` cáscara + `.ucas/.utoc`, el formato típico de Nexus). Los mods Zen se
  leen con **CUE4Parse** (`cue4parse.exe`), que requiere apuntar a la carpeta del
  juego (Ajustes) — usa el `global.utoc` del juego para resolver tipos.
- **Salida**: contenedor **Zen/IoStore** (`zzz_StellarTool_Merged_P.utoc/.ucas/.pak`,
  verificado) — formato nativo del juego. Sin retoc, pak legacy V11.
- **Merge de mods Zen**: se mergean **números, texto, enums, arrays, objetos y
  filas nuevas**. Los arrays escalares se reconstruyen incluso si la base está
  vacía. También se aplican filas borradas; como protección, se bloquean si un
  único mod pierde más del 25% de la tabla vanilla. Los arrays de structs sin
  una plantilla de layout se cuentan y avisan en el reporte.
- **Una tabla sin cambios aplicados no se emite**: el pak mergeado carga con
  prioridad máxima, así que empaquetar una copia de vanilla pisaría al mod de
  origen. Se deja fuera y se avisa (`merge_report.txt`).
- **Una tabla que falla la verificación no cancela las demás**: se elimina su
  salida parcial, se registra el motivo en `merge_report.txt` y el resultado
  indica que el mod de origen debe permanecer habilitado para aportar esa tabla.
- **Diagnóstico de fallos**: UAssetGUI no imprime sus errores (los copia al
  portapapeles). La tool los recupera y escribe un log por fallo en
  `%LOCALAPPDATA%\StellarTool\logs\` — pedilo siempre en un reporte de bug.
- **Baseline vanilla**: se construye con un clic desde Ajustes (lee todas las tablas
  del juego con CUE4Parse). También extraíble manualmente del juego con
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
