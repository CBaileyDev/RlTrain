<#
.SYNOPSIS
    Builds the RL Studio training engine.

.DESCRIPTION
    Wraps CMake so you do not have to open a "Developer PowerShell for VS" first.
    It locates your Visual Studio installation, imports the MSVC compiler environment,
    then configures and builds.

    Release is the default and is what you should use. On Windows, debug and release
    libtorch are not ABI-compatible, and the libtorch we ship is the release build.

.PARAMETER Config
    Build configuration. Release (default) or RelWithDebInfo.

.PARAMETER Target
    Build only one target, e.g. rl-engine or rlstudio_tests.

.PARAMETER Clean
    Delete the build directory first.

.PARAMETER Test
    Run the unit tests after building.

.PARAMETER Reconfigure
    Force CMake to re-run its configure step.

.EXAMPLE
    pwsh -File tools/build.ps1

.EXAMPLE
    pwsh -File tools/build.ps1 -Test

.EXAMPLE
    pwsh -File tools/build.ps1 -Clean -Config RelWithDebInfo
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Config = 'Release',
    [string]$Target,
    [switch]$Clean,
    [switch]$Test,
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$EngineDir = Join-Path $RepoRoot 'engine'
$BuildDir = Join-Path $EngineDir 'build'
$TorchDir = Join-Path $EngineDir 'libtorch'

function Import-MsvcEnvironment {
    <#
        CMake's Ninja generator needs cl.exe on PATH plus a pile of INCLUDE/LIB variables.
        Visual Studio only sets those inside its developer shell, so we run vcvars64.bat
        in a throwaway cmd and copy the resulting environment into this session.
    #>
    if (Get-Command cl -ErrorAction SilentlyContinue) {
        Write-Host '  MSVC environment already active.' -ForegroundColor DarkGray
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'Visual Studio is not installed. Run tools/setup.ps1 for instructions.'
    }

    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        throw "Visual Studio has no C++ workload installed. Add 'Desktop development with C++' in the Visual Studio Installer."
    }

    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

    Write-Host "  Loading MSVC environment from $vsPath" -ForegroundColor DarkGray
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue
        }
    }

    if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
        throw 'Failed to import the MSVC environment (cl.exe still not on PATH).'
    }
}

Write-Host ''
Write-Host "  Building RL Studio engine [$Config]" -ForegroundColor Cyan

if (-not (Test-Path (Join-Path $TorchDir 'share\cmake\Torch\TorchConfig.cmake'))) {
    throw "libtorch is missing from $TorchDir. Run tools/setup.ps1 first."
}

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host '  Removing previous build directory...' -ForegroundColor DarkGray
    Remove-Item $BuildDir -Recurse -Force
}

Import-MsvcEnvironment

$generator = if (Get-Command ninja -ErrorAction SilentlyContinue) { 'Ninja Multi-Config' } else { $null }

$needConfigure = $Reconfigure -or -not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))
if ($needConfigure) {
    Write-Host '  Configuring...' -ForegroundColor DarkGray
    $cfgArgs = @('-S', $EngineDir, '-B', $BuildDir, "-DCMAKE_PREFIX_PATH=$TorchDir")
    if ($generator) { $cfgArgs += @('-G', $generator) }
    & cmake @cfgArgs
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)." }
}

Write-Host '  Compiling...' -ForegroundColor DarkGray
$buildArgs = @('--build', $BuildDir, '--config', $Config, '--parallel')
if ($Target) { $buildArgs += @('--target', $Target) }
& cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }

$exe = Get-ChildItem -Path $BuildDir -Filter 'rl-engine.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($exe) {
    Write-Host "  Built $($exe.FullName)" -ForegroundColor Green
}

if ($Test) {
    Write-Host ''
    Write-Host '  Running tests...' -ForegroundColor Cyan
    Push-Location $BuildDir
    try {
        & ctest --build-config $Config --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Tests failed (exit $LASTEXITCODE)." }
        Write-Host '  All tests passed.' -ForegroundColor Green
    }
    finally { Pop-Location }
}

Write-Host ''
