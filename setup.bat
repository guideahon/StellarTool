@echo off
setlocal
cd /d "%~dp0"

REM Descarga los binarios externos requeridos a tools\ (ver tools\VERSIONS.md).
set REPAK_URL=https://github.com/trumank/repak/releases/download/v0.2.3/repak_cli-x86_64-pc-windows-msvc.zip
set UASSETGUI_URL=https://github.com/atenfyr/UAssetGUI/releases/download/v1.1.0/UAssetGUI.exe

if exist tools\repak.exe if exist tools\UAssetGUI.exe (
    echo [INFO] tools\repak.exe y tools\UAssetGUI.exe ya presentes.
    goto :done
)

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

echo [INFO] Verificando hashes...
powershell -NoProfile -Command "$ok=$true; $h1=(Get-FileHash tools\repak.exe -Algorithm SHA256).Hash; if($h1 -ne 'FCD538E5994B9BB833622D425AE346F4E0692F02D4B0025114A559F9B6286022'){Write-Host '[WARN] hash repak.exe distinto al esperado:' $h1; $ok=$false}; $h2=(Get-FileHash tools\UAssetGUI.exe -Algorithm SHA256).Hash; if($h2 -ne 'B7D75C0893F1A60E565853AE638BC21F2416CD12C2D9D854E297ABB87CEB3263'){Write-Host '[WARN] hash UAssetGUI.exe distinto al esperado:' $h2; $ok=$false}; if($ok){Write-Host '[INFO] Hashes OK.'}"

:done
echo [INFO] Setup completo. Compilar con: build.bat Release NOPAUSE
exit /b 0
