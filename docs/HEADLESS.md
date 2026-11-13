# Contrato headless de Stellar Tool

Este documento es la referencia operativa para agentes de IA y automatizaciones.
La aplicación GUI es una vista de las mismas operaciones: todo comando headless
se ejecuta sin crear una ventana Qt y escribe resultados en stdout. Los errores
se escriben con prefijo `[ERROR]` y un código distinto de cero.

## Regla para agentes

Antes de usar un comando:

1. Ejecutar `StellarTool.exe --headless --help` o consultar esta tabla.
2. Usar rutas absolutas entre comillas y conservar los archivos de origen.
3. No usar acciones que escriben en el juego sin confirmación explícita del usuario.
4. Interpretar el código de salida: `0` éxito, `2` argumentos/precondiciones,
   `3` timeout de operación, `4` resultado incompleto, `5` operación fallida.

## Cobertura por sección

| Sección GUI | Comandos headless | Efecto |
|---|---|---|
| Plugin CPS en juego | No aplica; `CPSPlugin` registra una página nativa dentro del proceso del juego. | Los controles runtime actúan sobre la sesión abierta. Para generar un mod estático con opciones compatibles del Builder, usar `build --answers`. |
| Mods / Cambios | `analyze` | Importa mods, analiza tablas y lista cambios/conflictos. |
| Conflictos / Merge | `merge` | Resuelve por prioridad o `--prefer` y genera el pak/ZIP. Si un cambio seleccionado de una tabla no se puede verificar, excluye la tabla completa para no ocultar el mod fuente y lo detalla en `merge_report.txt`. |
| Builder | `build`, `presets` | Compila desde JSON, `.stpreset` o preset guardado. La instalación/desinstalación directa se rechaza si el juego está abierto o Vortex administra la instalación: compila el ZIP e instálalo desde Vortex para que Vortex conserve sus hardlinks y manifiestos. |
| CNS Converter | `cns`, `replacer` | Convierte outfits y genera un ZIP instalable. `replacer` requiere que el mod fuente incluya el asset raíz del outfit; si solo incluye texturas/dependencias, se rechaza porque no puede reemplazar la malla. Repara referencias faltantes o `UnknownExport` de PhysicsAsset/ShadowPhysicsAsset desde la malla vanilla actual del destino, elimina imports `UnknownExport` huérfanos y valida el `.uasset` serializado; falla con detalle si queda una referencia sin resolver. `replacer --replace <outfit>` acepta los destinos de outfit vanilla (`Body`) que aparecen en el desplegable rápido; la interfaz avanzada también lista otros tipos de asset. `cns` aísla materiales/texturas compartidos y agrega controles de reparación compatibles con CNS. |
| CNS ID Fixer | `fixids` | Reporta IDs, rutas e integridad IoStore. Compara `Package_Id` con vanilla e informa posibles reemplazos, pero no convierte un override vanilla aislado porque puede ser intencional. Valida referencias personalizadas de los descriptores CNS (incluidos los roots de autor, no solo `/Game/OutfitMods`) contra assets IoStore; excluye los roots vanilla `Art`, `DLC_1` y `DLC_2`. Detecta filas de menú cuyo paquete no está instalado. Los conflictos entre mods muestran la ruta interna y cada contenedor; si hay descriptor CNS, incluye `DisplayName`, `UniqueFitID` y variantes. `--apply` repara `Container_Id`, reconstruye hashes reparables de `ContainerHeader`, convierte solo colisiones reales entre mods, mueve el descriptor completo si todas sus entradas carecen de assets requeridos y quita solo las entradas rotas de los JSON mixtos, guardando el original en `~mods/disabled/`. Las referencias parciales de material/textura se reportan sin ocultar un outfit que conserva sus assets requeridos. Si el índice no permite comprobar los assets, lo informa como incompleto y no modifica el descriptor por suposición. Usa `--apply --ids-only` para limitarse a `Container_Id`; `--fix-textures` es alias compatible. Devuelve código 4 si quedan conflictos, descriptores huérfanos, errores de integridad o chequeos incompletos. Requiere `~mods` activa configurada. |
| Baseline / Ajustes | `baseline`, `detect`, `status`, `storage` | Construye baseline, detecta juego, consulta instalación y gestiona la carpeta de trabajo temporal. |
| Live | `live` | Instala/desinstala bridge, consulta estado y publica valores. |
| ReShade | `reshade` | Lista, guarda, restaura, renombra, elimina, importa y exporta presets. |
| Partidas | `save-to-json`, `save-from-json`, `fix-save` | Convierte partidas y repara CNS. |
| Moveset granular | `moveset-catalog` + `build` | Analiza tablas/assets, genera catálogo de cambios y compila las selecciones del Builder. |
| Moveset legacy | `moveset` | Compatibilidad: lista/instala un bundle completo o desinstala lo instalado por la tool. |
| Patches declarativos | `patch-validate`, `patch-preview`, `patch-apply`, `patch-export` | Valida, expande, revisa, aplica o exporta reglas TOML sin ejecutar código externo. |

Las acciones de abrir carpetas, diálogos de archivo, temas, idioma y elementos
puramente visuales no tienen sentido headless; sus efectos sobre archivos o el
juego sí están cubiertos por los comandos de la tabla.

## Comandos

```bat
StellarTool.exe --headless analyze --mod "C:\mods\a.pak" --mod "C:\mods\b.pak" [--baseline "C:\baseline"]
StellarTool.exe --headless merge --mod "C:\mods\prioritario.pak" --mod "C:\mods\otro.pak" --out "C:\salida" [--prefer "prioritario"]
StellarTool.exe --headless build --answers "C:\config.json" --out "C:\salida"
StellarTool.exe --headless build --preset "Mi preset" --out "C:\salida"
StellarTool.exe --headless presets
StellarTool.exe --headless cns --mod "C:\outfit.zip" --out "C:\salida" [--name "Outfit"]
StellarTool.exe --headless replacer --mod "C:\outfit.zip" --out "C:\salida" --replace "Black Pearl" [--select "2"] [--omit-post-process-animblueprint]
StellarTool.exe --headless fixids --mod "C:\StellarBlade\SB\Content\Paks\~mods"
StellarTool.exe --headless fixids --mod "C:\StellarBlade\SB\Content\Paks\~mods" --apply
StellarTool.exe --headless fixids --mod "C:\StellarBlade\SB\Content\Paks\~mods" --apply --ids-only
StellarTool.exe --headless baseline --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless detect
StellarTool.exe --headless status
StellarTool.exe --headless storage --action status
StellarTool.exe --headless storage --action set --input "D:\StellarWork"
StellarTool.exe --headless storage --action clean
StellarTool.exe --headless storage --action reset
StellarTool.exe --headless moveset --mod "D:\Descargas\moveset" --action list
StellarTool.exe --headless moveset --mod "D:\Descargas\moveset" --action install --select scarlet-goddess --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless moveset --action uninstall
StellarTool.exe --headless moveset-catalog --mod "D:\Descargas\moveset" --out "C:\Temp\moveset-catalog.json" --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless patch-validate --input "C:\patches\CharacterTable.toml"
StellarTool.exe --headless patch-preview --input "C:\patches\CharacterTable.toml" --baseline "C:\baseline"
StellarTool.exe --headless patch-apply --input "C:\patches\CharacterTable.toml" --out "C:\salida" --baseline "C:\baseline"
StellarTool.exe --headless patch-export --mod "C:\mods\a.pak" --out "C:\patches"
```

Al convertir con `cns`, Stellar Tool reubica los materiales vanilla y las
texturas compartidas bajo el namespace del outfit para que reemplazos globales
no alteren su aspecto. El descriptor agrega opciones CNS para alternar las
texturas incluidas por el mod con copias originales cuando el asset lo permite,
y sliders para parámetros de rugosidad/metalicidad presentes en los materiales.
`replacer --replace "Fusion Suit"` agrega además las copias de Fusion NoAcc y
de los materiales de slot que el juego fuerza en el jefe final, si CUE4Parse
puede leerlos.
Por defecto, el replacer conserva el `PostProcessAnimBlueprint` de la malla
vanilla actual e ignora la referencia que traiga el mod fuente. Esto evita
arrastrar blueprints antiguos o irresolubles. `replacer
--omit-post-process-animblueprint` elimina la propiedad de la malla raíz y omite
del paquete el asset al que apuntaba la fuente. Se cancela si otro asset del
mod también lo referencia.
En cualquier replacer, la malla resultante conserva el esquema de la malla
vanilla actual del destino: exports auxiliares (incluidos morph targets) y
propiedades requeridas se mantienen, mientras los datos de la fuente se copian
solo cuando son compatibles con el tipo vanilla. Si `PhysicsAsset` o
`ShadowPhysicsAsset` faltan o son inválidos, se reconstruyen desde esa malla.
Después de serializar, la tool monta y extrae el IoStore generado, vuelve a leer
el `.uasset` y comprueba que no haya referencias activas fuera de rango o a
cadenas de imports desconocidas. Las filas de import sin referencias activas
se permiten. La conversión falla si no puede extraer la malla vanilla o si
alguna referencia usada continúa inválida.
La conversión escribe en la carpeta indicada y no modifica la entrada ni los
paks del juego. CNS ID Fixer aplica aislamiento a los contenedores IoStore
activos que comparten `Package_Id` entre mods. Intenta las conversiones una por
una y prepara las aceptadas antes de mover archivos. Los contenedores que aún
choquen o tengan errores de integridad irreparables se mueven junto con sus
archivos compañeros a respaldos recuperables bajo `~mods/disabled/` con extensión
`.disabled`.

El escaneo compara también con los paquetes originales y valida las rutas
personalizadas de los descriptores `.dekcns.json` contra los assets presentes en
IoStore. Incluye roots de autor como `/Game/PaulR0013Mods`, `/Game/Xiaolongnu` y
`/Game/OUTFITTK930`; deja los roots vanilla `Art`, `DLC_1` y `DLC_2` para el
chequeo de `Package_Id`. Informa filas aunque les falte la malla, el material o
la textura. Si todas las entradas del descriptor carecen de sus assets requeridos,
`--apply` respalda el JSON completo. En descriptores mixtos, quita solo las
entradas rotas y respalda el JSON original antes de escribir la versión filtrada.
Los faltantes parciales de materiales/texturas se reportan sin ocultar una entrada
que conserva sus assets requeridos. Para
restaurar cualquier asset ausente, hay que habilitar y volver a desplegar el mod
fuente en Vortex. Si algún índice de paquete no se puede leer, hay un PAK legacy
o un UCAS sin UTOC, o no hay contenedores activos, el chequeo se informa como
incompleto y no mueve el descriptor por una suposición.

Un reemplazo vanilla individual se informa como posible override, pero no se
convierte ni se desactiva automáticamente: puede ser un replacer intencional.
Solo las colisiones `Package_Id` reales entre mods se aíslan de forma automática.
El escaneo final devuelve éxito cuando no quedan colisiones entre mods, filas CNS
huérfanas, IDs duplicados ni errores de integridad. El comando requiere
`--apply`, que confirma este cambio productivo.
`--fix-textures` se conserva como alias compatible y también requiere `--apply`.
CNS debe estar instalado para desplegar los outfits convertidos.

El escaneo de `fixids` compara además los `Package_Id` activos con los de los
contenedores originales bajo `Paks`, excluyendo `~mods`, `LogicMods`, backups y
stages de Stellar Tool. En colisiones entre mods lista la ruta interna del asset,
cada contenedor fuente y, si hay un descriptor CNS coincidente, su nombre visible,
`UniqueFitID` y variantes. Esto permite distinguir dos paquetes que registran el
mismo outfit visible. El reporte muestra también las rutas vanilla afectadas
cuando `retoc` puede listarlas, para detectar un reemplazo de textura o material
global aunque no haya otro mod que use ese mismo ID. Al aplicar la reparación,
intenta convertir a CNS los outfits compatibles y verifica que bajen las
referencias en conflicto antes de conservar el cambio.

`fixids --apply` actúa directamente sobre la instalación: instala los outfits
CNS que el conversor puede reparar y mueve sus fuentes originales a
`~mods/disabled/`. Los conflictos restantes y los contenedores con errores de
integridad no reparables también se mueven a respaldos con extensión
`.disabled`; el reporte identifica cada ruta. Requiere confirmación explícita
mediante `--apply`; el código de salida es 4 si el escaneo final aún detecta
problemas.

Además, `fixids --apply` verifica los `.utoc` con `retoc verify`. Si el único
fallo es el hash del chunk `ContainerHeader` y la tabla de chunks sigue presente,
reconstruye el contenedor con el mismo contenido y las mismas rutas de assets,
comprueba que se conserven los `Package_Id`, y verifica el resultado antes de
reemplazarlo. La familia anterior queda en `~mods/disabled/` con extensión
`.disabled`. Los errores de integridad que no se puedan reparar se mueven a
respaldos deshabilitados para que no sigan afectando la instalación.
`--apply --ids-only` solo corrige `Container_Id` y no reconstruye
los contenedores.

`--input` también puede ser una carpeta `.stpatch` o un archivo comprimido
`.stpatch`/`.zip` que contenga `manifest.toml` opcional y uno o más `.toml`.
El manifest declara `game`, `requires_game_version`, `dependencies` e
`incompatibilities`; se valida el juego y la versión detectada. Las reglas se
aplican en orden alfabético. La
sintaxis admite el formato literal heredado de automod y operaciones seguras:

```toml
[meta]
table = "CharacterTable"
game = "Stellar Blade"
requires_game_version = "1.4.1"

[Player]
AttackSpeed = { op = "multiply", value = 1.5 }
MaxBurstGauge = { op = "set", value = 1800, expect = 1600 }

[row_regex:^Enemy_.*]
MaxHP = { op = "multiply", value = 2 }
```

Las operaciones disponibles son `set`, `add`, `multiply`, `clamp` (con
`min`/`max`) y `toggle`. No se ejecutan los scripts multilínea ni los
`patchlets` de Scala/Python de automod. Regex solo selecciona nombres de fila.
Los arrays/objetos literales se pueden leer y se intentan escribir sobre la
estructura original; si UAssetGUI no los verifica fielmente, la tabla se
excluye del pak y el reporte lo detalla. Esto conserva la protección existente
para mods Zen.
Los cambios se convierten en `ChangeItem` y pasan por el mismo merge,
verificación round-trip y reporte que un mod normal.
`merge_report.txt` agrupa cada tabla debajo del mod que la aporta, muestra
`applied`/`skipped` o el motivo `EXCLUDED`, y agrega un enlace de Nexus cuando
el nombre del archivo conserva el ID de mod. No se deben desactivar los mods
asociados a tablas excluidas o no escritas.

`moveset-catalog` reconoce carpetas con tríos IoStore completos, deriva familia,
tier y `aggro`, y compara cada variante con el juego. El JSON resultante se
guarda fuera del repositorio y se referencia desde `answers.moveset.catalog`;
`answers.moveset.selectedChanges` contiene los IDs elegidos. El comando `build`
aplica los cambios escalares, rechaza conflictos no resueltos y copia los
bundles Zen de assets seleccionados sin modificar los archivos originales.
`moveset` queda solo como compatibilidad para instalar una variante completa.

`build --answers` también acepta `harderBosses` y `harderEnemies` como objetos
independientes. Cada uno puede incluir `health`, `attack`, `size`, `removeShield`,
`xp`, `staggerImmunity` y sus multiplicadores (`healthMultiplier`,
`attackMultiplier`, `sizeMultiplier`, `xpMultiplier`). Los sliders de la UI
generan exactamente ese JSON, por lo que el build headless es equivalente.
También están disponibles `shieldRegen`, `shieldDamageReduction`, `stamina`,
`staminaRegen`, `attackSpeed`, `moveSpeed` y `drops`, con sus multiplicadores.
`challengeProfile` acepta `glassCannon`, `attrition` o `endurance` y completa
los dos objetos sin pisar valores explícitos.

### Partidas

```bat
StellarTool.exe --headless save-to-json --input "C:\saves\DekCNS.sav" --out "C:\work\DekCNS.json" --indent 2
StellarTool.exe --headless save-from-json --input "C:\work\DekCNS.json" --out "C:\saves\DekCNS_fixed.sav"
StellarTool.exe --headless fix-save --input "C:\saves\DekCNS.sav"
```

`save-from-json` crea el backup automático del destino si ya existe. `fix-save`
repara sobrescribiendo la partida y también crea backup. Verificar siempre la
partida dentro del juego; el formato se obtiene por reverse engineering.

### ReShade

ReShade necesita que el juego esté configurado con `--game` o en la preferencia
persistida. `list` no modifica nada.

```bat
StellarTool.exe --headless reshade --action list --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless reshade --action save --name "Mi preset"
StellarTool.exe --headless reshade --action restore --name "Mi preset"
StellarTool.exe --headless reshade --action rename --old-name "Viejo" --new-name "Nuevo"
StellarTool.exe --headless reshade --action delete --name "Mi preset"
StellarTool.exe --headless reshade --action import --input "C:\presets\externo.ini"
StellarTool.exe --headless reshade --action export --name "Mi preset" --input "C:\presets\copia.ini"
```

`restore` respalda `ReShade.ini` y el preset activo antes de cambiarlo.

### Live

```bat
StellarTool.exe --headless live --action status --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless live --action install --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless live --action set --fov 90 --speed 1.25 --jump 1.5 --fov-enabled
StellarTool.exe --headless live --action reset
StellarTool.exe --headless live --action uninstall
```

`live set` publica un archivo de solicitud para el bridge UE4SS; no inyecta
memoria ni garantiza que el juego esté ejecutándose. Los límites se aplican en
`LiveService` y nuevamente en Lua. `install`, `uninstall`, `reset` y `set` son
acciones con efectos y requieren autorización del usuario.

## Limitaciones deliberadas

- El plugin CPS opera sobre tablas ya cargadas en un proceso del juego; no tiene
  modo headless. `build --answers` genera archivos de mod y no modifica una
  sesión en ejecución.
- No hay una operación headless para abrir carpetas, mostrar diálogos, cambiar
  temas/idioma o abrir la ventana de Settings.
- `analyze` y `merge` requieren los binarios externos y, para mods Zen, la ruta
  del juego; `save-*` requiere el Python embebido incluido en el paquete.
- Los comandos no deben recibir assets oficiales, claves AES ni partidas del
  usuario en reportes o commits.
