@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Usage: build.bat [Debug|Release] [NOPAUSE]   (default: Release)
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
set NO_PAUSE=0
if /I "%2"=="NOPAUSE" set NO_PAUSE=1

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set GENERATOR=Visual Studio 16 2019
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022

if not exist "%CMAKE%" ( echo [ERROR] CMake not found. & goto :failed )
if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" ( echo [ERROR] Qt6 not found at %QT_DIR% & goto :failed )

taskkill /F /IM StellarTool.exe >nul 2>&1

if not exist build mkdir build
"%CMAKE%" -S . -B build -G "%GENERATOR%" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 ( echo === Configure FAILED === & goto :failed )

"%CMAKE%" --build build --config %CONFIG% -- /maxcpucount:4
if errorlevel 1 ( echo === %CONFIG% Build FAILED === & goto :failed )

set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
set "EXE_PATH=%~dp0build\%CONFIG%\StellarTool.exe"
if not exist "%EXE_PATH%" ( echo [ERROR] %EXE_PATH% missing & goto :failed )

set DEPLOY_FLAG=--release
if /I "%CONFIG%"=="Debug" set DEPLOY_FLAG=--debug
"%WINDEPLOYQT%" %DEPLOY_FLAG% --qmldir "%~dp0qml" --no-translations --compiler-runtime "%EXE_PATH%" >nul
if errorlevel 1 ( echo === windeployqt FAILED === & goto :failed )

REM Copiar externos junto al exe (los que existan). oo2core NO se copia (Oodle,
REM propietaria; retoc no lo necesita para el flujo de esta tool).
if not exist "%~dp0build\%CONFIG%\tools" mkdir "%~dp0build\%CONFIG%\tools"
for %%F in (repak.exe retoc.exe UAssetGUI.exe StellarBlade.usmap) do (
    if exist "%~dp0tools\%%F" copy /Y "%~dp0tools\%%F" "%~dp0build\%CONFIG%\tools\" >nul
)

echo.
echo === Build complete ===
echo %CONFIG%: build\%CONFIG%\StellarTool.exe
if "%NO_PAUSE%"=="0" pause
exit /b 0

:failed
if "%NO_PAUSE%"=="0" pause
exit /b 1
