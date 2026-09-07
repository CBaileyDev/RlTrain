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

# CUDA 13.0's host_config.h refuses any MSVC newer than the Visual Studio 2022 toolset,
# and forcing it through with -allow-unsupported-compiler makes nvcc's cudafe++ crash on
# the newer STL headers. So when we are building against CUDA libtorch we must select a
# 14.4x toolset even on a machine whose default is newer.
$script:MaxCudaToolset = [version]'14.50'

function Get-CudaRoot {
    <#
        Returns the CUDA Toolkit path using FORWARD SLASHES, or $null.

        Forward slashes matter: libtorch bundles its own copy of the old FindCUDA module,
        which pastes the path into a CMake string. A Windows path like
        "C:\Program Files\NVIDIA..." then fails to parse with "Invalid character escape '\P'".
    #>
    $base = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA'
    if (-not (Test-Path $base)) { return $null }
    $newest = Get-ChildItem $base -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^v\d+\.\d+$' } |
        Sort-Object { [version]($_.Name.TrimStart('v')) } -Descending |
        Select-Object -First 1
    if (-not $newest) { return $null }
    return $newest.FullName.Replace('\', '/')
}

function Import-MsvcEnvironment {
    <#
        CMake's Ninja generator needs cl.exe on PATH plus a pile of INCLUDE/LIB variables.
        Visual Studio only sets those inside its developer shell, so we run vcvars64.bat
        in a throwaway cmd and copy the resulting environment into this session.

        When CUDA is in play we additionally pin the toolset version, because the newest
        installed MSVC is often too new for nvcc.
    #>
    param([switch]$ForCuda)

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'Visual Studio is not installed. Run tools/setup.ps1 for instructions.'
    }

    $installs = & $vswhere -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    if (-not $installs) {
        throw "Visual Studio has no C++ workload installed. Add 'Desktop development with C++' in the Visual Studio Installer."
    }

    # Collect every (installation, toolset version) pair available on this machine.
    $candidates = foreach ($inst in $installs) {
        $toolsetDir = Join-Path $inst.installationPath 'VC\Tools\MSVC'
        if (-not (Test-Path $toolsetDir)) { continue }
        foreach ($t in Get-ChildItem $toolsetDir -Directory -ErrorAction SilentlyContinue) {
            $parsed = $null
            if (-not [version]::TryParse($t.Name, [ref]$parsed)) { continue }
            [pscustomobject]@{
                Install = $inst.installationPath
                Display = $inst.displayName
                Full    = $parsed
                Short   = '{0}.{1}' -f $parsed.Major, ([string]$parsed.Minor).PadLeft(2, '0').Substring(0, 2)
            }
        }
    }

    if (-not $candidates) { throw 'No MSVC toolset found in any Visual Studio installation.' }

    $chosen = $null
    if ($ForCuda) {
        $chosen = $candidates | Where-Object { $_.Full -lt $script:MaxCudaToolset } |
            Sort-Object Full -Descending | Select-Object -First 1
        if (-not $chosen) {
            $newest = ($candidates | Sort-Object Full -Descending | Select-Object -First 1).Full
            throw @"
No CUDA-compatible MSVC toolset is installed.
  Newest found: $newest
  Needed:       anything below $script:MaxCudaToolset (the Visual Studio 2022 v143 toolset)

CUDA cannot compile against a newer toolset. Two ways forward:
  1. In the Visual Studio Installer, add "MSVC v143 - VS 2022 C++ x64/x86 build tools".
  2. Build without CUDA:  pwsh -File tools/setup.ps1 -Cpu   (training will be much slower)
"@
        }
    }
    else {
        $chosen = $candidates | Sort-Object Full -Descending | Select-Object -First 1
    }

    $vcvars = Join-Path $chosen.Install 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

    Write-Host "  MSVC $($chosen.Full) from $($chosen.Display)" -ForegroundColor DarkGray

    # Ask vcvars for that exact toolset so a newer default does not win.
    $verArg = "-vcvars_ver=$($chosen.Full.Major).$($chosen.Full.Minor)"
    cmd /c "`"$vcvars`" $verArg >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue
        }
    }

    $cl = Get-Command cl -ErrorAction SilentlyContinue
    if (-not $cl) { throw 'Failed to import the MSVC environment (cl.exe still not on PATH).' }
    Write-Verbose "cl.exe: $($cl.Source)"
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

# Is this a CUDA libtorch? If so we need a CUDA-compatible compiler and toolkit paths.
$isCudaTorch = Test-Path (Join-Path $TorchDir 'lib\torch_cuda.dll')
$cudaRoot = if ($isCudaTorch) { Get-CudaRoot } else { $null }

if ($isCudaTorch -and -not $cudaRoot) {
    throw @'
This is the CUDA build of libtorch, but no CUDA Toolkit is installed.

libtorch ships its own CUDA runtime DLLs, but its CMake config still calls find_package(CUDA)
while configuring, so the toolkit has to be present to build.

  Fix:  winget install --id Nvidia.CUDA --version 13.0
  Or:   pwsh -File tools/setup.ps1 -Cpu    (rebuild against CPU libtorch instead)
'@
}

Import-MsvcEnvironment -ForCuda:$isCudaTorch

if ($cudaRoot) {
    Write-Host "  CUDA $cudaRoot" -ForegroundColor DarkGray
    $env:PATH = "$cudaRoot/bin;$env:PATH"
    $env:CUDA_PATH = $cudaRoot
}

$generator = if (Get-Command ninja -ErrorAction SilentlyContinue) { 'Ninja Multi-Config' } else { $null }

$needConfigure = $Reconfigure -or -not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))
if ($needConfigure) {
    Write-Host '  Configuring...' -ForegroundColor DarkGray
    # Forward slashes throughout: libtorch's vendored FindCUDA module chokes on backslashes.
    $cfgArgs = @(
        '-S', $EngineDir.Replace('\', '/'),
        '-B', $BuildDir.Replace('\', '/'),
        "-DCMAKE_PREFIX_PATH=$($TorchDir.Replace('\','/'))"
    )
    if ($generator) { $cfgArgs += @('-G', $generator) }
    if ($cudaRoot) {
        $cfgArgs += "-DCUDA_TOOLKIT_ROOT_DIR=$cudaRoot"
        # 89 = Ada Lovelace (RTX 40 series). Widen this if you target other GPUs.
        $cfgArgs += '-DCMAKE_CUDA_ARCHITECTURES=89'
    }
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
