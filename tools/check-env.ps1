<#
.SYNOPSIS
    Checks that everything RL Studio needs is present, and explains how to fix whatever is missing.

.DESCRIPTION
    Run this any time something will not build or the app complains. It never changes anything.
    Every check prints one of:
        [ OK ]    found and usable
        [WARN]    missing, but only blocks an optional feature
        [FAIL]    missing, and the project will not build or run without it

.EXAMPLE
    pwsh -File tools/check-env.ps1
#>
[CmdletBinding()]
param(
    [switch]$Json
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent $PSScriptRoot
$results = [System.Collections.Generic.List[object]]::new()

function Add-Result {
    param(
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][ValidateSet('OK', 'WARN', 'FAIL')][string]$Status,
        [string]$Detail = '',
        [string]$Fix = ''
    )
    $results.Add([pscustomobject]@{
        Name   = $Name
        Status = $Status
        Detail = $Detail
        Fix    = $Fix
    })
}

function Write-Results {
    $colors = @{ OK = 'Green'; WARN = 'Yellow'; FAIL = 'Red' }
    Write-Host ''
    Write-Host '  RL Studio environment check' -ForegroundColor Cyan
    Write-Host '  ---------------------------' -ForegroundColor Cyan
    foreach ($r in $results) {
        $tag = '[{0}]' -f $r.Status.PadRight(4)
        Write-Host "  $tag " -ForegroundColor $colors[$r.Status] -NoNewline
        Write-Host $r.Name.PadRight(24) -NoNewline
        Write-Host $r.Detail -ForegroundColor DarkGray
        if ($r.Status -ne 'OK' -and $r.Fix) {
            Write-Host ('         -> ' + $r.Fix) -ForegroundColor DarkYellow
        }
    }
    $fails = @($results | Where-Object Status -eq 'FAIL').Count
    $warns = @($results | Where-Object Status -eq 'WARN').Count
    Write-Host ''
    if ($fails -gt 0) {
        Write-Host "  $fails blocking problem(s), $warns warning(s)." -ForegroundColor Red
        Write-Host '  Run tools/setup.ps1 to fix most of these automatically.' -ForegroundColor DarkYellow
    }
    elseif ($warns -gt 0) {
        Write-Host "  Ready to build. $warns optional feature(s) unavailable." -ForegroundColor Yellow
    }
    else {
        Write-Host '  Everything is ready.' -ForegroundColor Green
    }
    Write-Host ''
}

# --- Operating system ---------------------------------------------------------
if ($IsWindows -or $env:OS -eq 'Windows_NT') {
    $os = (Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue).Caption
    Add-Result 'Windows' 'OK' $os
}
else {
    Add-Result 'Windows' 'FAIL' 'Not running on Windows' 'RL Studio targets Windows. RocketSim collision-mesh dumping is Windows-only.'
}

# --- CPU / RAM ----------------------------------------------------------------
try {
    $cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
    $ramGb = [math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB, 1)
    $cores = $cpu.NumberOfLogicalProcessors
    $status = if ($cores -ge 8) { 'OK' } else { 'WARN' }
    Add-Result 'CPU / RAM' $status "$($cpu.Name.Trim()), $cores threads, $ramGb GB RAM" `
        'Fewer than 8 threads will make training slow. Lower env.num_arenas in your config.'
}
catch { Add-Result 'CPU / RAM' 'WARN' 'Could not query hardware' }

# --- NVIDIA GPU ---------------------------------------------------------------
$nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
if ($nvidiaSmi) {
    $gpu = (& nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader 2>$null | Select-Object -First 1)
    if ($gpu) {
        $driver = ($gpu -split ',')[-1].Trim()
        $driverMajor = [int]($driver -split '\.')[0]
        # CUDA 13.0 runtime requires driver >= 580 on Windows.
        if ($driverMajor -ge 580) {
            Add-Result 'NVIDIA GPU' 'OK' $gpu.Trim()
        }
        else {
            Add-Result 'NVIDIA GPU' 'WARN' $gpu.Trim() `
                "Driver $driver is older than 580, which CUDA 13.0 requires. Update your NVIDIA driver, or training falls back to CPU."
        }
    }
    else { Add-Result 'NVIDIA GPU' 'WARN' 'nvidia-smi present but returned nothing' 'Training will fall back to CPU and be far slower.' }
}
else {
    Add-Result 'NVIDIA GPU' 'WARN' 'No NVIDIA GPU detected' `
        'Training will run on CPU, which is dramatically slower. Everything still works.'
}

# --- CUDA Toolkit -------------------------------------------------------------
# libtorch ships every CUDA runtime DLL it needs, but its CMake config still calls
# find_package(CUDA) at configure time, so the toolkit must be installed to BUILD.
$cudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA'
$cudaVersions = @()
if (Test-Path $cudaRoot) {
    $cudaVersions = @(Get-ChildItem $cudaRoot -Directory -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Name)
}
if ($cudaVersions.Count -gt 0) {
    Add-Result 'CUDA Toolkit' 'OK' ($cudaVersions -join ', ')
}
else {
    Add-Result 'CUDA Toolkit' 'FAIL' 'Not installed' `
        'The CUDA build of libtorch cannot be configured without it. Run: winget install --id Nvidia.CUDA --version 13.0   (or re-run setup.ps1 -Cpu for a slower CPU-only build)'
}

# --- Visual Studio C++ toolchain ---------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsPath) {
        $vsName = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property displayName 2>$null
        Add-Result 'MSVC C++ toolchain' 'OK' "$vsName at $vsPath"
    }
    else {
        Add-Result 'MSVC C++ toolchain' 'FAIL' 'Visual Studio found but the C++ workload is not installed' `
            'In the Visual Studio Installer, add the "Desktop development with C++" workload.'
    }
}
else {
    Add-Result 'MSVC C++ toolchain' 'FAIL' 'Visual Studio not found' `
        'Install Visual Studio Build Tools with the "Desktop development with C++" workload: winget install Microsoft.VisualStudio.2022.BuildTools'
}

# --- CMake / Ninja / Git ------------------------------------------------------
foreach ($tool in @(
    @{ Name = 'CMake'; Cmd = 'cmake'; Arg = '--version'; MinVersion = [version]'3.21'; Fix = 'winget install Kitware.CMake' },
    @{ Name = 'Ninja'; Cmd = 'ninja'; Arg = '--version'; MinVersion = $null; Fix = 'winget install Ninja-build.Ninja' },
    @{ Name = 'Git';   Cmd = 'git';   Arg = '--version'; MinVersion = $null; Fix = 'winget install Git.Git' }
)) {
    $cmd = Get-Command $tool.Cmd -ErrorAction SilentlyContinue
    if ($cmd) {
        $verLine = (& $tool.Cmd $tool.Arg 2>$null | Select-Object -First 1)
        $ok = $true
        if ($tool.MinVersion) {
            if ($verLine -match '(\d+)\.(\d+)\.(\d+)') {
                $found = [version]("{0}.{1}.{2}" -f $Matches[1], $Matches[2], $Matches[3])
                $ok = $found -ge $tool.MinVersion
            }
        }
        if ($ok) { Add-Result $tool.Name 'OK' $verLine }
        else { Add-Result $tool.Name 'FAIL' "$verLine (need $($tool.MinVersion)+)" $tool.Fix }
    }
    else { Add-Result $tool.Name 'FAIL' 'Not on PATH' $tool.Fix }
}

# --- Rust / Cargo (needed only for the desktop app) --------------------------
$cargoExe = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
$cargoCmd = Get-Command cargo -ErrorAction SilentlyContinue
if ($cargoCmd -or (Test-Path $cargoExe)) {
    $cargoPath = if ($cargoCmd) { $cargoCmd.Source } else { $cargoExe }
    $ver = (& $cargoPath --version 2>$null)
    Add-Result 'Rust / Cargo' 'OK' $ver
}
else {
    Add-Result 'Rust / Cargo' 'FAIL' 'Not installed' `
        'Needed to build the desktop app. Run: winget install Rustlang.Rustup   (the engine alone builds without it)'
}

# --- Node.js ------------------------------------------------------------------
$node = Get-Command node -ErrorAction SilentlyContinue
if ($node) {
    $nodeVer = (& node --version 2>$null)
    $major = [int]($nodeVer -replace '^v(\d+).*', '$1')
    if ($major -ge 20) { Add-Result 'Node.js' 'OK' $nodeVer }
    else { Add-Result 'Node.js' 'FAIL' "$nodeVer (need 20+)" 'winget install OpenJS.NodeJS.LTS' }
}
else { Add-Result 'Node.js' 'FAIL' 'Not on PATH' 'winget install OpenJS.NodeJS.LTS' }

# --- WebView2 (Tauri runtime) -------------------------------------------------
$webview2Keys = @(
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
    'HKLM:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}',
    'HKCU:\SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}'
)
$wv = $webview2Keys | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($wv) {
    $wvVer = (Get-ItemProperty $wv -ErrorAction SilentlyContinue).pv
    Add-Result 'WebView2 runtime' 'OK' "version $wvVer"
}
else {
    Add-Result 'WebView2 runtime' 'WARN' 'Not detected' `
        'Bundled with Windows 11. If the app window is blank, install the Evergreen WebView2 Runtime from Microsoft.'
}

# --- Vendored RocketSim -------------------------------------------------------
$rsPath = Join-Path $RepoRoot 'engine\third_party\RocketSim\src\RocketSim.h'
if (Test-Path $rsPath) { Add-Result 'RocketSim source' 'OK' 'engine/third_party/RocketSim' }
else {
    Add-Result 'RocketSim source' 'FAIL' 'Missing' `
        'Run tools/setup.ps1, which clones ZealanL/RocketSim into engine/third_party/RocketSim.'
}

# --- libtorch -----------------------------------------------------------------
$torchCmake = Join-Path $RepoRoot 'engine\libtorch\share\cmake\Torch\TorchConfig.cmake'
if (Test-Path $torchCmake) {
    $torchDll = Join-Path $RepoRoot 'engine\libtorch\lib\torch_cuda.dll'
    $flavor = if (Test-Path $torchDll) { 'CUDA build' } else { 'CPU-only build' }
    Add-Result 'libtorch' 'OK' "engine/libtorch ($flavor)"
}
else {
    Add-Result 'libtorch' 'FAIL' 'Missing' 'Run tools/setup.ps1 to download libtorch 2.14.0+cu130 (about 3 GB).'
}

# --- Arena collision meshes (dumped by the user, never redistributed) --------
$meshDir = Join-Path $RepoRoot 'engine\assets\collision_meshes'
if (Test-Path $meshDir) {
    $meshCount = @(Get-ChildItem $meshDir -Recurse -File -ErrorAction SilentlyContinue).Count
    if ($meshCount -gt 0) { Add-Result 'Collision meshes' 'OK' "$meshCount file(s) in engine/assets/collision_meshes" }
    else {
        Add-Result 'Collision meshes' 'FAIL' 'Folder exists but is empty' `
            'The engine cannot simulate without them. See docs/getting-started.md, or use the in-app onboarding wizard.'
    }
}
else {
    Add-Result 'Collision meshes' 'FAIL' 'Not dumped yet' `
        'Start Rocket League, run RLArenaCollisionDumper, then copy its collision-meshes output into engine/assets/collision_meshes. The app wizard walks you through it.'
}

# --- Rocket League install (only needed to dump meshes) ----------------------
$rlPaths = @(
    'C:\Program Files\Epic Games\rocketleague',
    'C:\Program Files (x86)\Steam\steamapps\common\rocketleague'
)
$rl = $rlPaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($rl) { Add-Result 'Rocket League' 'OK' $rl }
else {
    Add-Result 'Rocket League' 'WARN' 'Not found in the usual locations' `
        'Only needed once, to dump arena collision meshes. Not needed to train or to run the app.'
}

# --- RLViser (optional external visualizer) ----------------------------------
$rlviser = Join-Path $RepoRoot 'engine\assets\rlviser\rlviser.exe'
if (Test-Path $rlviser) { Add-Result 'RLViser' 'OK' 'engine/assets/rlviser/rlviser.exe' }
else {
    Add-Result 'RLViser' 'WARN' 'Not installed' `
        'Optional. The built-in 3D viewer works without it. tools/setup.ps1 -WithRlviser downloads it.'
}

# --- Disk space ---------------------------------------------------------------
try {
    $drive = (Get-Item $RepoRoot).PSDrive
    $freeGb = [math]::Round($drive.Free / 1GB, 1)
    if ($freeGb -ge 20) { Add-Result 'Disk space' 'OK' "$freeGb GB free on $($drive.Name):" }
    else { Add-Result 'Disk space' 'WARN' "$freeGb GB free on $($drive.Name):" 'libtorch plus build output needs roughly 15 GB. Training runs add more.' }
}
catch { Add-Result 'Disk space' 'WARN' 'Could not determine free space' }

if ($Json) {
    $results | ConvertTo-Json -Depth 4
}
else {
    Write-Results
}

exit (@($results | Where-Object Status -eq 'FAIL').Count -gt 0 ? 1 : 0)
