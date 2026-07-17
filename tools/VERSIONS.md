# Binarios externos requeridos

Colocar en esta carpeta (`tools/`). `build.bat` los copia junto al exe.
También se pueden apuntar con variables de entorno `ST_REPAK` / `ST_UASSETGUI`.

| Binario | Fuente | Versión instalada | SHA-256 | Uso |
|---|---|---|---|---|
| `repak.exe` | https://github.com/trumank/repak/releases | v0.2.3 (win64) | `FCD538E5994B9BB833622D425AE346F4E0692F02D4B0025114A559F9B6286022` | unpack/pack de .pak (UE4.26 = versión de pak V11) |
| `UAssetGUI.exe` | https://github.com/atenfyr/UAssetGUI/releases | v1.1.0 | `B7D75C0893F1A60E565853AE638BC21F2416CD12C2D9D854E297ABB87CEB3263` | tojson/fromjson de .uasset (motor de tablas: `VER_UE4_26`) |

Al actualizar cualquiera de los dos: correr un round-trip de prueba
(unpack → tojson → fromjson → pack) con un mod conocido antes de commitear,
y anotar acá la versión y el hash SHA-256 del binario.
