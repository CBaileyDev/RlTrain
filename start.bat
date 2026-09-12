@echo off
setlocal
cd /d "%~dp0"

set "SOURCE_APP=%CD%\app\src-tauri\target\release\rl-studio.exe"
set "SOURCE_ENGINE=%CD%\engine\build\bin\rl-engine.exe"
set "PORTABLE_DIR=%CD%\artifacts\RLStudio"
set "PORTABLE_APP=%PORTABLE_DIR%\rl-studio.exe"
set "PORTABLE_ENGINE=%PORTABLE_DIR%\engine\build\bin\rl-engine.exe"

if exist "%SOURCE_APP%" if exist "%SOURCE_ENGINE%" (
  set "RL_STUDIO_HOME=%CD%"
  start "" /D "%CD%" "%SOURCE_APP%"
  exit /b 0
)

if exist "%PORTABLE_APP%" if exist "%PORTABLE_ENGINE%" (
  set "RL_STUDIO_HOME=%PORTABLE_DIR%"
  start "" /D "%PORTABLE_DIR%" "%PORTABLE_APP%"
  exit /b 0
)

echo RL Studio is not built yet.
echo.
if not exist "%SOURCE_APP%" if not exist "%PORTABLE_APP%" (
  echo   App:    pwsh -File tools\build-app.ps1
)
if not exist "%SOURCE_ENGINE%" if not exist "%PORTABLE_ENGINE%" (
  echo   Engine: pwsh -File tools\build.ps1
)
echo.
echo Or package a portable folder with:
echo   pwsh -File tools\package.ps1
echo.
pause
exit /b 1
