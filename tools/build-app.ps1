[CmdletBinding()]
param([switch]$DebugBuild)
$ErrorActionPreference = 'Stop'
$StudioRoot = Split-Path -Parent $PSScriptRoot
& npm --prefix (Join-Path $StudioRoot 'app') run check
if ($LASTEXITCODE -ne 0) { throw 'Frontend checks failed.' }
& npm --prefix (Join-Path $StudioRoot 'app') run build
if ($LASTEXITCODE -ne 0) { throw 'Frontend build failed.' }
$CargoArgs = @('build', '--manifest-path', (Join-Path $StudioRoot 'app/src-tauri/Cargo.toml'), '--features', 'custom-protocol')
if (-not $DebugBuild) { $CargoArgs += '--release' }
& cargo @CargoArgs
if ($LASTEXITCODE -ne 0) { throw 'Desktop build failed.' }
Write-Host 'Desktop app built with embedded frontend.' -ForegroundColor Green
