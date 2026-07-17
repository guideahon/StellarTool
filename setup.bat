@echo off
setlocal
cd /d "%~dp0"

REM Descarga los binarios externos requeridos a tools\ (ver tools\VERSIONS.md).
set REPAK_URL=https://github.com/trumank/repak/releases/download/v0.2.3/repak_cli-x86_64-pc-windows-msvc.zip
REM Se requiere >= 1.1.1 (el canal experimental): 1.1.0 estable parsea las
REM DataTables de Stellar Blade como RawExport y el diff no funciona.
set UASSETGUI_URL=https://github.com/atenfyr/UAssetGUI/releases/download/experimental-latest/UAssetGUI.exe
set RETOC_URL=https://github.com/trumank/retoc/releases/download/v0.1.5/retoc_cli-x86_64-pc-windows-msvc.zip

if not exist tools mkdir tools

if not exist tools\repak.exe (
    echo [INFO] Descargando repak v0.2.3...
    powershell -NoProfile -Command "Invoke-WebRequest '%REPAK_URL%' -OutFile '%TEMP%\repak_st.zip'; Expand-Archive '%TEMP%\repak_st.zip' '%TEMP%\repak_st' -Force; Copy-Item '%TEMP%\repak_st\repak.exe' 'tools\repak.exe' -Force"
    if not exist tools\repak.exe ( echo [ERROR] No se pudo descargar repak.exe & exit /b 1 )
)

if not exist tools\UAssetGUI.exe (
    echo [INFO] Descargando UAssetGUI v1.1.0...
    powershell -NoProfile -Command "Invoke-WebRequest '%UASSETGUI_URL%' -OutFile 'tools\UAssetGUI.exe'"
    if not exist tools\UAssetGUI.exe ( echo [ERROR] No se pudo descargar UAssetGUI.exe & exit /b 1 )
)

if not exist tools\retoc.exe (
    echo [INFO] Descargando retoc v0.1.5...
    powershell -NoProfile -Command "Invoke-WebRequest '%RETOC_URL%' -OutFile '%TEMP%\retoc_st.zip'; Expand-Archive '%TEMP%\retoc_st.zip' '%TEMP%\retoc_st' -Force; Copy-Item (Get-ChildItem '%TEMP%\retoc_st' -Recurse -Filter retoc.exe)[0].FullName 'tools\retoc.exe' -Force"
    if not exist tools\retoc.exe ( echo [ERROR] No se pudo descargar retoc.exe & exit /b 1 )
)

echo [INFO] Verificando hash de repak...
powershell -NoProfile -Command "$h=(Get-FileHash tools\repak.exe -Algorithm SHA256).Hash; if($h -ne 'FCD538E5994B9BB833622D425AE346F4E0692F02D4B0025114A559F9B6286022'){Write-Host '[WARN] hash repak.exe distinto al esperado:' $h} else {Write-Host '[INFO] Hash OK.'}"

if not exist tools\StellarBlade.usmap (
    echo [WARN] Falta tools\StellarBlade.usmap ^(mappings del juego^). Sin el,
    echo [WARN] las tablas se decodifican sin nombres de propiedades. Ver tools\VERSIONS.md.
)

echo [INFO] Setup completo. Compilar con: build.bat Release NOPAUSE
exit /b 0
