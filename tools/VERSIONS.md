# Binarios externos requeridos

Colocar en esta carpeta (`tools/`). `build.bat` los copia junto al exe.
También se pueden apuntar con variables de entorno `ST_REPAK` / `ST_UASSETGUI`.

| Binario | Fuente | Versión instalada | Uso |
|---|---|---|---|
| `repak.exe` | https://github.com/trumank/repak/releases | v0.2.3 (win64) | unpack/pack de .pak legacy (UE4.26 = versión de pak V11) |
| `UAssetGUI.exe` | https://github.com/atenfyr/UAssetGUI/releases (tag `experimental-latest`) | 1.1.1 experimental | tojson/fromjson de .uasset (`VER_UE4_26` + usmap). **La 1.1.0 estable NO sirve**: parsea las DataTables de Stellar Blade como RawExport. |
| `retoc.exe` | https://github.com/trumank/retoc/releases | v0.1.5 | Zen/IoStore: extraer tablas vanilla del juego (`to-legacy`) y empaquetar el merge (`to-zen` + `verify`) |

## Mappings (`StellarBlade.usmap`)

Colocar `StellarBlade.usmap` en esta carpeta (o apuntar con env `ST_USMAP`). Sin él,
UAssetGUI produce JSON sin nombres de propiedades y el diff pierde legibilidad.
Se consigue en la comunidad de modding de Stellar Blade (dump de mappings del juego,
p.ej. generado con UE4SS). No se versiona en el repo.

Al actualizar cualquiera de los dos: correr un round-trip de prueba
(unpack → tojson → fromjson → pack) con un mod conocido antes de commitear,
y anotar acá la versión y el hash SHA-256 del binario.
