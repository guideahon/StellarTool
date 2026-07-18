# Binarios externos requeridos

Colocar en esta carpeta (`tools/`). `build.bat` los copia junto al exe.
También se pueden apuntar con variables de entorno `ST_REPAK` / `ST_UASSETGUI`.

| Binario | Fuente | Versión instalada | Uso |
|---|---|---|---|
| `repak.exe` | https://github.com/trumank/repak/releases | v0.2.3 (win64) | unpack/pack de .pak legacy (UE4.26 = versión de pak V11) |
| `UAssetGUI.exe` | https://github.com/atenfyr/UAssetGUI/releases (tag `experimental-latest`) | 1.1.1 experimental | tojson/fromjson de .uasset (`VER_UE4_26` + usmap). **La 1.1.0 estable NO sirve**: parsea las DataTables de Stellar Blade como RawExport. |
| `retoc.exe` | https://github.com/trumank/retoc/releases | v0.1.5 | Zen/IoStore: extraer tablas vanilla del juego (`to-legacy`) y empaquetar el merge (`to-zen` + `verify`) |
| `cue4parse.exe` | https://github.com/joric/CUE4Parse.CLI/releases | cli-0.1.8 | Lee contenedores Zen/IoStore de mods (que retoc no puede revertir) a JSON. Requiere el `global.utoc` del juego + usmap para resolver tipos. |

## Mappings (`StellarBlade.usmap`)

`setup.bat` lo descarga automáticamente a esta carpeta desde el archivo público de
la comunidad: https://github.com/TheNaeem/Unreal-Mappings-Archive
(ruta `Stellar Blade/1.4.1/Mappings.usmap`). También se puede apuntar con env `ST_USMAP`.

Sin él, UAssetGUI decodifica las DataTables de Stellar Blade sin nombres de
propiedades (aparecen como RawExport) y el diff no funciona. **No se versiona en
este repo** (mapping de la comunidad, licencia no declarada); solo se descarga.

Al actualizar cualquiera de los dos: correr un round-trip de prueba
(unpack → tojson → fromjson → pack) con un mod conocido antes de commitear,
y anotar acá la versión y el hash SHA-256 del binario.
