# Binarios externos requeridos

Colocar en esta carpeta (`tools/`). `build.bat` los copia junto al exe.
También se pueden apuntar con variables de entorno `ST_REPAK` / `ST_UASSETGUI`.

| Binario | Fuente | Versión instalada | Uso |
|---|---|---|---|
| `repak.exe` | https://github.com/trumank/repak/releases | v0.2.3 (win64) | unpack/pack de .pak legacy (UE4.26 = versión de pak V11) |
| `UAssetGUI.exe` | https://github.com/atenfyr/UAssetGUI/releases (tag `experimental-latest`) | 1.1.1 experimental | tojson/fromjson de .uasset (`VER_UE4_26` + usmap). **La 1.1.0 estable NO sirve**: parsea las DataTables de Stellar Blade como RawExport. |
| `retoc.exe` | https://github.com/trumank/retoc/releases | v0.1.5 | Zen/IoStore: extraer tablas vanilla del juego (`to-legacy`) y empaquetar el merge (`to-zen` + `verify`) |
| `cue4parse.exe` | https://github.com/joric/CUE4Parse.CLI/releases | cli-0.1.8 | Lee contenedores Zen/IoStore de mods (que retoc no puede revertir) a JSON. Requiere el `global.utoc` del juego + usmap para resolver tipos. |

## Datos del conversor CNS

`setup.bat` descarga a `tools/CNSRepacker/data/` las bases
`rootAssetToInfo.txt`, `assetToRootAsset.txt`, `assetToImportAsset.txt` y
`excludedAssets.txt` del proyecto
[StellarBladeCNSRepacker](https://gitlab.com/DeronFer/cnsrepacker), junto con su
licencia MIT. También descarga el `retoc.exe` usado por ese proyecto, necesario
porque soporta `to-legacy --mount-folder`; retoc CLI 0.1.5 no incluye esa opción.
Stellar Tool porta la lógica a C++ y no distribuye ni ejecuta
`CNSRepacker.exe`. Las variables `ST_CNSREPACKER_DATA` y `ST_CNS_RETOC`
permiten elegir copias compatibles para desarrollo.

## Mappings (`StellarBlade.usmap`)

`setup.bat` lo descarga automáticamente a esta carpeta desde el archivo público de
la comunidad: https://github.com/TheNaeem/Unreal-Mappings-Archive
(ruta `Stellar Blade/1.4.1/Mappings.usmap`). También se puede apuntar con env `ST_USMAP`,
o cargar/descargar uno desde **Ajustes → Mappings** (queda en QSettings `usmapOverride`;
la descarga por versión pega contra el mismo archivo de la comunidad).

Al invocar UAssetGUI se pasa la **ruta** del `.usmap`, no su nombre: resolver por
nombre exige que el archivo esté en `%LOCALAPPDATA%/UAssetGUI/Mappings` y falla en
silencio si no está. Ver ARCHITECTURE.md §7.

Sin él, UAssetGUI decodifica las DataTables de Stellar Blade sin nombres de
propiedades (aparecen como RawExport) y el diff no funciona. **No se versiona en
este repo** (mapping de la comunidad, licencia no declarada); solo se descarga.

Al actualizar cualquiera de los dos: correr un round-trip de prueba
(unpack → tojson → fromjson → pack) con un mod conocido antes de commitear,
y anotar acá la versión y el hash SHA-256 del binario.
