@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Genera el zip distribuible de Stellar Tool (exe + runtime Qt + tools\).
REM Uso: package.bat [NOPAUSE]
set NO_PAUSE=0
if /I "%1"=="NOPAUSE" set NO_PAUSE=1

REM Version desde CMakeLists: linea "project(StellarTool VERSION x.y.z LANGUAGES CXX)"
set VERSION=
for /f "tokens=3" %%V in ('findstr /c:"project(StellarTool VERSION" CMakeLists.txt') do set VERSION=%%V
if "%VERSION%"=="" set VERSION=0.0.0

echo [INFO] Asegurando binarios externos (setup.bat)...
call "%~dp0setup.bat"
if errorlevel 1 ( echo [ERROR] setup fallo & goto :fail )

echo [INFO] Compilando Release...
call "%~dp0build.bat" Release NOPAUSE
if errorlevel 1 ( echo [ERROR] build fallo & goto :fail )

set SRC=%~dp0build\Release
set EXE=%SRC%\StellarTool.exe
if not exist "%EXE%" ( echo [ERROR] falta %EXE% & goto :fail )

REM Verificar que los externos requeridos quedaron junto al exe
for %%F in (repak.exe retoc.exe UAssetGUI.exe cue4parse.exe) do (
    if not exist "%SRC%\tools\%%F" ( echo [ERROR] falta tools\%%F en el build & goto :fail )
)
if not exist "%SRC%\tools\StellarBlade.usmap" echo [WARN] falta StellarBlade.usmap; el zip funcionara pero sin nombres de propiedades.

REM Nunca empaquetar la DLL propietaria de Oodle
if exist "%SRC%\tools\oo2core_9_win64.dll" del /f /q "%SRC%\tools\oo2core_9_win64.dll" >nul 2>&1

set OUTDIR=%~dp0dist
set ZIP=%OUTDIR%\StellarTool-%VERSION%.zip
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if exist "%ZIP%" del /f /q "%ZIP%"

echo [INFO] Comprimiendo %ZIP% ...
REM Empaqueta el contenido de build\Release en la raiz del zip (bsdtar de Windows).
tar -a -c -f "%ZIP%" -C "%SRC%" .
if errorlevel 1 ( echo [ERROR] zip fallo & goto :fail )

for %%A in ("%ZIP%") do set ZSIZE=%%~zA
echo.
echo === Release listo ===
echo %ZIP%  (%ZSIZE% bytes)
if "%NO_PAUSE%"=="0" pause
exit /b 0

:fail
if "%NO_PAUSE%"=="0" pause
exit /b 1
