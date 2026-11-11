@echo off
setlocal
set "PLUGIN_DIR=%~dp0."
set "REPO_DIR=%~dp0.."
set "SDK_DIR=%~dp0..\CPS-SDK"
set "BUILD_DIR=%REPO_DIR%\build\CPSPlugin"

if not exist "%SDK_DIR%\CMakeLists.txt" (
  echo CPS SDK not found: "%SDK_DIR%"
  exit /b 1
)

cmake -S "%PLUGIN_DIR%" -B "%BUILD_DIR%" -A x64 -D"CPS_SDK_DIR=%SDK_DIR%"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b %errorlevel%

echo.
echo Plugin: "%BUILD_DIR%\Release\stellar_tool_gameplay.cps"
