@echo off
setlocal
cd /d "%~dp0"

REM Descarga los binarios externos requeridos a tools\ (ver tools\VERSIONS.md).
set REPAK_URL=https://github.com/trumank/repak/releases/download/v0.2.3/repak_cli-x86_64-pc-windows-msvc.zip
REM Se requiere >= 1.1.1 (el canal experimental): 1.1.0 estable parsea las
REM DataTables de Stellar Blade como RawExport y el diff no funciona.
set UASSETGUI_URL=https://github.com/atenfyr/UAssetGUI/releases/download/experimental-latest/UAssetGUI.exe
set RETOC_URL=https://github.com/trumank/retoc/releases/download/v0.1.5/retoc_cli-x86_64-pc-windows-msvc.zip
REM Mappings de Stellar Blade (comunidad; NO se versionan en el repo).
set USMAP_URL=https://raw.githubusercontent.com/TheNaeem/Unreal-Mappings-Archive/main/Stellar%%20Blade/1.4.1/Mappings.usmap
REM CUE4Parse CLI: lee contenedores Zen/IoStore (mods de Nexus) a JSON.
set CUE4_URL=https://github.com/joric/CUE4Parse.CLI/releases/download/cli-0.1.8/CUE4Parse.CLI-0.1.8-Win64-bin.zip
REM Runtime de Visual C++ (VS 2015-2022 x64). Se empaqueta en el zip para que
REM el .exe arranque en equipos sin el runtime instalado (VCRUNTIME140.dll).
set VCREDIST_URL=https://aka.ms/vs/17/release/vc_redist.x64.exe
REM Bases de rutas/dependencias del CNSRepacker original (MIT). El conversor
REM nativo C++ usa estos datos, no el ejecutable Java.
set CNS_DATA_URL=https://gitlab.com/DeronFer/cnsrepacker/-/raw/main/data
set CNS_LICENSE_URL=https://gitlab.com/DeronFer/cnsrepacker/-/raw/main/LICENSE.txt
set CNS_RETOC_URL=https://gitlab.com/DeronFer/cnsrepacker/-/raw/main/tools/retoc/retoc.exe

if not exist tools mkdir tools

if not exist tools\CNSRepacker\data\rootAssetToInfo.txt (
    echo [INFO] Descargando datos MIT de CNSRepacker...
    if not exist tools\CNSRepacker\data mkdir tools\CNSRepacker\data
    powershell -NoProfile -Command "$names='rootAssetToInfo.txt','assetToRootAsset.txt','assetToImportAsset.txt','excludedAssets.txt'; foreach($n in $names){Invoke-WebRequest ('%CNS_DATA_URL%/'+$n) -OutFile ('tools\\CNSRepacker\\data\\'+$n)}; Invoke-WebRequest '%CNS_LICENSE_URL%' -OutFile 'tools\\CNSRepacker\\LICENSE.txt'"
    if not exist tools\CNSRepacker\data\assetToImportAsset.txt ( echo [ERROR] No se pudieron descargar los datos de CNSRepacker & exit /b 1 )
)
if not exist tools\CNSRepacker\retoc.exe (
    echo [INFO] Descargando retoc compatible con CNS --mount-folder...
    powershell -NoProfile -Command "Invoke-WebRequest '%CNS_RETOC_URL%' -OutFile 'tools\\CNSRepacker\\retoc.exe'"
    if not exist tools\CNSRepacker\retoc.exe ( echo [ERROR] Falta retoc compatible con CNS & exit /b 1 )
)

if not exist tools\repak.exe (
    echo [INFO] Descargando repak v0.2.3...
    powershell -NoProfile -Command "Invoke-WebRequest '%REPAK_URL%' -OutFile '%TEMP%\repak_st.zip'; Expand-Archive '%TEMP%\repak_st.zip' '%TEMP%\repak_st' -Force; Copy-Item '%TEMP%\repak_st\repak.exe' 'tools\repak.exe' -Force"
    if not exist tools\repak.exe ( echo [ERROR] No se pudo descargar repak.exe & exit /b 1 )
)

if not exist tools\UAssetGUI.exe (
    echo [INFO] Descargando UAssetGUI experimental 1.1.1+ ...
    powershell -NoProfile -Command "Invoke-WebRequest '%UASSETGUI_URL%' -OutFile 'tools\UAssetGUI.exe'"
    if not exist tools\UAssetGUI.exe ( echo [ERROR] No se pudo descargar UAssetGUI.exe & exit /b 1 )
)

if not exist tools\retoc.exe (
    echo [INFO] Descargando retoc v0.1.5...
    powershell -NoProfile -Command "Invoke-WebRequest '%RETOC_URL%' -OutFile '%TEMP%\retoc_st.zip'; Expand-Archive '%TEMP%\retoc_st.zip' '%TEMP%\retoc_st' -Force; Copy-Item (Get-ChildItem '%TEMP%\retoc_st' -Recurse -Filter retoc.exe)[0].FullName 'tools\retoc.exe' -Force"
    if not exist tools\retoc.exe ( echo [ERROR] No se pudo descargar retoc.exe & exit /b 1 )
)

if not exist tools\cue4parse.exe (
    echo [INFO] Descargando CUE4Parse.CLI 0.1.8...
    powershell -NoProfile -Command "Invoke-WebRequest '%CUE4_URL%' -OutFile '%TEMP%\cue4_st.zip'; Expand-Archive '%TEMP%\cue4_st.zip' '%TEMP%\cue4_st' -Force; Copy-Item (Get-ChildItem '%TEMP%\cue4_st' -Recurse -Filter cue4parse.exe)[0].FullName 'tools\cue4parse.exe' -Force"
    if not exist tools\cue4parse.exe ( echo [WARN] No se pudo descargar cue4parse.exe; los mods Zen no se podran leer. )
)

if not exist tools\vc_redist.x64.exe (
    echo [INFO] Descargando runtime de Visual C++ x64...
    powershell -NoProfile -Command "try { Invoke-WebRequest '%VCREDIST_URL%' -OutFile 'tools\vc_redist.x64.exe' } catch { }"
    if not exist tools\vc_redist.x64.exe ( echo [WARN] No se pudo descargar vc_redist.x64.exe; el zip no incluira el runtime de VC++. )
)

echo [INFO] Verificando hash de repak...
powershell -NoProfile -Command "try { $h=(Get-FileHash 'tools\repak.exe' -Algorithm SHA256).Hash; if($h -eq 'FCD538E5994B9BB833622D425AE346F4E0692F02D4B0025114A559F9B6286022'){Write-Host '[INFO] Hash OK.'} else {Write-Host '[WARN] hash repak.exe distinto al esperado:' $h} } catch { Write-Host '[INFO] Verificacion de hash omitida.' }"

if not exist tools\StellarBlade.usmap call :get_usmap
goto :after_usmap
:get_usmap
echo [INFO] Descargando StellarBlade.usmap [mappings de la comunidad]
powershell -NoProfile -Command "try { Invoke-WebRequest '%USMAP_URL%' -OutFile 'tools\StellarBlade.usmap' } catch { }"
if exist tools\StellarBlade.usmap ( echo [INFO] Mappings descargados. & goto :eof )
echo [WARN] No se pudo descargar StellarBlade.usmap; las tablas se decodifican sin
echo [WARN] nombres de propiedades. Ver tools\VERSIONS.md.
goto :eof
:after_usmap

echo [INFO] Setup completo. Compilar con: build.bat Release NOPAUSE
exit /b 0
