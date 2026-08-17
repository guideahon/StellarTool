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
| Mods / Cambios | `analyze` | Importa mods, analiza tablas y lista cambios/conflictos. |
| Conflictos / Merge | `merge` | Resuelve por prioridad o `--prefer` y genera el pak/ZIP. |
| Builder | `build`, `presets` | Compila desde JSON, `.stpreset` o preset guardado. |
| CNS Converter | `cns`, `replacer` | Convierte outfits y genera la salida instalable. |
| CNS ID Fixer | `fixids` | Reporta o corrige IDs; requiere `--apply` para escribir. |
| Baseline / Ajustes | `baseline`, `detect`, `status` | Construye baseline, detecta juego y consulta instalación. |
| Live | `live` | Instala/desinstala bridge, consulta estado y publica valores. |
| ReShade | `reshade` | Lista, guarda, restaura, renombra, elimina, importa y exporta presets. |
| Partidas | `save-to-json`, `save-from-json`, `fix-save` | Convierte partidas y repara CNS. |
| Moveset Fusion/Scarlet/Raven | `moveset` | Lista e instala una variante precompilada, o quita la instalada por la tool. |

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
StellarTool.exe --headless replacer --mod "C:\outfit-cns.zip" --out "C:\salida" --replace "Black Pearl" [--select "2"]
StellarTool.exe --headless fixids --mod "C:\StellarBlade\SB\Content\Paks\~mods" [--apply]
StellarTool.exe --headless baseline --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless detect
StellarTool.exe --headless status
StellarTool.exe --headless moveset --mod "D:\Descargas\moveset" --action list
StellarTool.exe --headless moveset --mod "D:\Descargas\moveset" --action install --select scarlet-goddess --game "C:\Steam\steamapps\common\StellarBlade"
StellarTool.exe --headless moveset --action uninstall
```

`moveset` reconoce carpetas con un trío IoStore completo (`.pak`, `.ucas` y
`.utoc`) y deriva la familia, tier y variante `aggro` de su nombre. `install`
solo copia la variante elegida a `SB\Content\Paks\~mods`; rechaza colisiones y
guarda un registro propio para que `uninstall` quite únicamente esos archivos.
Los binarios originales nunca se agregan al repositorio ni se modifican.

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

- No hay una operación headless para abrir carpetas, mostrar diálogos, cambiar
  temas/idioma o abrir la ventana de Settings.
- `analyze` y `merge` requieren los binarios externos y, para mods Zen, la ruta
  del juego; `save-*` requiere el Python embebido incluido en el paquete.
- Los comandos no deben recibir assets oficiales, claves AES ni partidas del
  usuario en reportes o commits.
