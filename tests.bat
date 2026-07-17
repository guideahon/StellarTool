@echo off
setlocal
cd /d "%~dp0"

REM Usage: tests.bat [Release|Debug]   (default: Release)
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set GENERATOR=Visual Studio 16 2019
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022

if not exist build_tests mkdir build_tests
"%CMAKE%" -S . -B build_tests -G "%GENERATOR%" -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%" -DBUILD_TESTS=ON
if errorlevel 1 ( echo === Configure FAILED === & exit /b 1 )

"%CMAKE%" --build build_tests --config %CONFIG% --target StellarToolTests -- /maxcpucount:4
if errorlevel 1 ( echo === Build FAILED === & exit /b 1 )

set PATH=%QT_DIR%\bin;%PATH%
"build_tests\%CONFIG%\StellarToolTests.exe"
if errorlevel 1 ( echo === TESTS FAILED === & exit /b 1 )
echo === Tests OK ===
exit /b 0
