[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$StudioRoot = Split-Path -Parent $PSScriptRoot
$StudioExe = Join-Path $StudioRoot 'app/src-tauri/target/release/rl-studio.exe'
if (-not (Test-Path -LiteralPath $StudioExe)) { throw 'Build the app first: pwsh -File tools/build-app.ps1' }
if (-not (Test-Path -LiteralPath (Join-Path $StudioRoot 'engine/build/bin/rl-engine.exe'))) { throw 'Build the engine first: pwsh -File tools/build.ps1' }
Start-Process -FilePath $StudioExe -WorkingDirectory $StudioRoot -WindowStyle Hidden
