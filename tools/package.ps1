[CmdletBinding()]
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$StudioRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Test
    & (Join-Path $PSScriptRoot 'build-app.ps1')
}
$PackageDir = Join-Path $StudioRoot 'artifacts/RLStudio'
$EngineSource = Join-Path $StudioRoot 'engine/build/bin'
$EngineTarget = Join-Path $PackageDir 'engine/build/bin'
New-Item -ItemType Directory -Path $EngineTarget -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $StudioRoot 'app/src-tauri/target/release/rl-studio.exe') -Destination $PackageDir -Force
Copy-Item -LiteralPath (Join-Path $EngineSource 'rl-engine.exe') -Destination $EngineTarget -Force
Get-ChildItem -LiteralPath $EngineSource -Filter '*.dll' | Copy-Item -Destination $EngineTarget -Force
foreach ($folder in @('configs','docs','licenses')) {
    Copy-Item -LiteralPath (Join-Path $StudioRoot $folder) -Destination $PackageDir -Recurse -Force
}
$AssetsSource = Join-Path $StudioRoot 'engine/assets'
if (Test-Path -LiteralPath $AssetsSource) {
    $AssetsTarget = Join-Path $PackageDir 'engine/assets'
    if (Test-Path -LiteralPath $AssetsTarget) {
        Remove-Item -LiteralPath $AssetsTarget -Recurse -Force
    }
    New-Item -ItemType Directory -Path $AssetsTarget -Force | Out-Null
    Copy-Item -Path (Join-Path $AssetsSource '*') -Destination $AssetsTarget -Recurse -Force
}
foreach ($file in @('README.md','LICENSE','THIRD_PARTY.md')) {
    Copy-Item -LiteralPath (Join-Path $StudioRoot $file) -Destination $PackageDir -Force
}
$LicenseDir = Join-Path $PackageDir 'licenses'
New-Item -ItemType Directory -Path $LicenseDir -Force | Out-Null
$LicenseSources = @{
    'RocketSim' = 'engine/third_party/RocketSim'; 'Bullet' = 'engine/third_party/RocketSim/libsrc/bullet3-3.24';
    'libtorch' = 'engine/libtorch'; 'nlohmann-json' = 'engine/build/_deps/nlohmann_json-src';
    'flatbuffers' = 'engine/build/_deps/flatbuffers-src'; 'svelte' = 'app/node_modules/svelte';
    'three' = 'app/node_modules/three'; 'uplot' = 'app/node_modules/uplot';
    'marked' = 'app/node_modules/marked'; 'dompurify' = 'app/node_modules/dompurify'
}
foreach ($entry in $LicenseSources.GetEnumerator()) {
    $destination = Join-Path $LicenseDir $entry.Key
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $StudioRoot $entry.Value) -File |
        Where-Object { $_.Name -match '^(LICENSE|COPYING|NOTICE)' } |
        Copy-Item -Destination $destination -Force
}
Get-FileHash -LiteralPath (Join-Path $PackageDir 'rl-studio.exe'), (Join-Path $EngineTarget 'rl-engine.exe') |
    Select-Object Algorithm,Hash,@{Name='File';Expression={Split-Path $_.Path -Leaf}} |
    ConvertTo-Json | Set-Content (Join-Path $PackageDir 'build-hashes.json')
Write-Host "Portable build: $PackageDir" -ForegroundColor Green
