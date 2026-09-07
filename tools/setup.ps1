<#
.SYNOPSIS
    One-command setup for RL Studio: fetches dependencies and builds the training engine.

.DESCRIPTION
    Safe to re-run. Every step checks whether its work is already done and skips if so.

    Steps:
      1. Verify the C++ toolchain, CMake and Git are present
      2. Fetch the vendored RocketSim source
      3. Download and unpack libtorch (about 3 GB, only once)
      4. Install Rust and the Node dependencies for the desktop app
      5. Optionally download RLViser, the external 3D visualizer
      6. Configure and build the engine

    What this script deliberately does NOT do: dump Rocket League arena collision meshes.
    Those are derived from copyrighted game files, so you must dump them yourself from your own
    installation. The app's onboarding wizard walks you through it, and docs/getting-started.md
    explains it in full.

.PARAMETER SkipBuild
    Fetch dependencies but do not compile the engine.

.PARAMETER SkipApp
    Skip Rust and Node setup. Use this if you only want the command-line trainer.

.PARAMETER WithRlviser
    Also download the RLViser external visualizer.

.PARAMETER Cpu
    Download the CPU-only build of libtorch instead of the CUDA build.
    Much smaller, but training will be far slower.

.EXAMPLE
    pwsh -File tools/setup.ps1

.EXAMPLE
    pwsh -File tools/setup.ps1 -WithRlviser
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$SkipApp,
    [switch]$WithRlviser,
    [switch]$Cpu
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$RepoRoot = Split-Path -Parent $PSScriptRoot
$EngineDir = Join-Path $RepoRoot 'engine'

# --- Pinned versions. Changing these is a deliberate act; see docs/internal/GROUNDING.md. ---
$LibtorchVersion = '2.14.0'
$LibtorchCuda = 'cu130'   # cu128 does not exist for 2.14.0. CUDA 13.0 needs NVIDIA driver 580+.
$RocketSimRepo = 'https://github.com/ZealanL/RocketSim.git'
$RlviserRepo = 'VirxEC/rlviser'

$script:StepNumber = 0
function Write-Step {
    param([string]$Message)
    $script:StepNumber++
    Write-Host ''
    Write-Host "  [$script:StepNumber] $Message" -ForegroundColor Cyan
}
function Write-Ok { param([string]$m) Write-Host "      $m" -ForegroundColor Green }
function Write-Skip { param([string]$m) Write-Host "      $m" -ForegroundColor DarkGray }
function Write-Note { param([string]$m) Write-Host "      $m" -ForegroundColor DarkYellow }

function Test-Tool {
    param([string]$Name)
    $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

Write-Host ''
Write-Host '  RL Studio setup' -ForegroundColor White
Write-Host '  ===============' -ForegroundColor White
Write-Host "  Repository: $RepoRoot" -ForegroundColor DarkGray

# ---------------------------------------------------------------------------
Write-Step 'Checking the C++ toolchain'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "Visual Studio is not installed. Install the C++ build tools first:`n      winget install Microsoft.VisualStudio.2022.BuildTools --override `"--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended`""
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "Visual Studio is installed but the C++ workload is missing.`n      Open the Visual Studio Installer and add 'Desktop development with C++'."
}
Write-Ok "MSVC found at $vsPath"

foreach ($t in @('cmake', 'git')) {
    if (-not (Test-Tool $t)) { throw "$t is not on PATH. Install it and re-run this script." }
}
Write-Ok ((& cmake --version | Select-Object -First 1))
if (Test-Tool 'ninja') { Write-Ok 'Ninja found (fast builds enabled)' }
else { Write-Note 'Ninja not found; falling back to the Visual Studio generator (slower).' }

# ---------------------------------------------------------------------------
Write-Step 'Fetching RocketSim (the Rocket League physics simulator)'

$rsDir = Join-Path $EngineDir 'third_party\RocketSim'
if (Test-Path (Join-Path $rsDir 'src\RocketSim.h')) {
    Write-Skip 'Already present.'
}
else {
    New-Item -ItemType Directory -Force -Path (Split-Path $rsDir) | Out-Null
    Write-Host '      Cloning...' -ForegroundColor DarkGray
    & git clone --depth 1 $RocketSimRepo $rsDir
    if ($LASTEXITCODE -ne 0) { throw 'Failed to clone RocketSim.' }
    Remove-Item (Join-Path $rsDir '.git') -Recurse -Force -ErrorAction SilentlyContinue
    Write-Ok 'RocketSim vendored (MIT licensed).'
}

# ---------------------------------------------------------------------------
Write-Step 'Checking for the CUDA Toolkit'

# libtorch bundles every CUDA runtime DLL it needs at run time, but TorchConfig.cmake
# calls find_package(CUDA) during configure, so a real toolkit must be installed to build.
$cudaRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA'
$haveCuda = (Test-Path $cudaRoot) -and
            (@(Get-ChildItem $cudaRoot -Directory -ErrorAction SilentlyContinue).Count -gt 0)

if ($Cpu) {
    Write-Skip 'Building CPU-only; the CUDA Toolkit is not needed.'
}
elseif ($haveCuda) {
    $found = (Get-ChildItem $cudaRoot -Directory | Select-Object -ExpandProperty Name) -join ', '
    Write-Ok "CUDA Toolkit $found"
}
else {
    Write-Note 'The CUDA Toolkit is not installed.'
    Write-Note 'It is required to BUILD against the CUDA libtorch, even though libtorch ships its own runtime DLLs.'
    if (Test-Tool 'winget') {
        Write-Host '      Installing CUDA Toolkit 13.0 (large download)...' -ForegroundColor DarkGray
        & winget install --id Nvidia.CUDA --version 13.0 --silent --accept-package-agreements --accept-source-agreements --disable-interactivity
        if (Test-Path $cudaRoot) { Write-Ok 'CUDA Toolkit installed.' }
        else {
            Write-Note 'Automatic install did not succeed.'
            Write-Note 'Install it from https://developer.nvidia.com/cuda-downloads, or re-run this script with -Cpu.'
        }
    }
    else {
        Write-Note 'winget is unavailable. Install CUDA 13.0 manually, or re-run with -Cpu.'
    }
}

# ---------------------------------------------------------------------------
Write-Step 'Downloading libtorch (the PyTorch C++ library)'

$torchDir = Join-Path $EngineDir 'libtorch'
$torchConfig = Join-Path $torchDir 'share\cmake\Torch\TorchConfig.cmake'

if (Test-Path $torchConfig) {
    Write-Skip 'Already present.'
}
else {
    $compute = if ($Cpu) { 'cpu' } else { $LibtorchCuda }
    $zipName = "libtorch-win-shared-with-deps-$LibtorchVersion%2B$compute.zip"
    $url = "https://download.pytorch.org/libtorch/$compute/$zipName"
    $downloadDir = Join-Path $EngineDir '.download'
    New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
    $zipPath = Join-Path $downloadDir 'libtorch.zip'

    Write-Note "About 3 GB. This is the slow part; it only happens once."
    Write-Host "      $url" -ForegroundColor DarkGray

    # curl.exe shows a progress meter and resumes; Invoke-WebRequest is very slow for large files.
    if (Test-Tool 'curl.exe') {
        & curl.exe -L --fail --retry 3 --retry-delay 5 -o $zipPath $url
        if ($LASTEXITCODE -ne 0) { throw "Download failed (curl exit $LASTEXITCODE)." }
    }
    else {
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $url -OutFile $zipPath
    }

    Write-Host '      Extracting...' -ForegroundColor DarkGray
    Expand-Archive -Path $zipPath -DestinationPath $EngineDir -Force
    Remove-Item $zipPath -Force -ErrorAction SilentlyContinue

    if (-not (Test-Path $torchConfig)) { throw "Extraction finished but $torchConfig is missing." }
    $flavor = if ($Cpu) { 'CPU-only' } else { "CUDA $LibtorchCuda" }
    Write-Ok "libtorch $LibtorchVersion ($flavor) ready."
}

# ---------------------------------------------------------------------------
if ($WithRlviser) {
    Write-Step 'Downloading RLViser (optional external 3D visualizer)'
    $rlviserDir = Join-Path $EngineDir 'assets\rlviser'
    $rlviserExe = Join-Path $rlviserDir 'rlviser.exe'
    if (Test-Path $rlviserExe) {
        Write-Skip 'Already present.'
    }
    else {
        New-Item -ItemType Directory -Force -Path $rlviserDir | Out-Null
        try {
            $release = Invoke-RestMethod "https://api.github.com/repos/$RlviserRepo/releases/latest" -Headers @{ 'User-Agent' = 'rl-studio-setup' }
            $asset = $release.assets | Where-Object { $_.name -match 'windows|win.*\.zip$|\.exe$' } | Select-Object -First 1
            if (-not $asset) { throw 'No Windows asset in the latest release.' }
            $tmp = Join-Path $rlviserDir $asset.name
            Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $tmp
            if ($tmp -match '\.zip$') {
                Expand-Archive -Path $tmp -DestinationPath $rlviserDir -Force
                Remove-Item $tmp -Force
            }
            Write-Ok "RLViser $($release.tag_name) downloaded."
            Write-Note 'On first launch RLViser extracts Rocket League assets for full-fidelity visuals.'
        }
        catch {
            Write-Note "Could not download RLViser automatically: $($_.Exception.Message)"
            Write-Note "Download it manually from https://github.com/$RlviserRepo/releases into engine/assets/rlviser/"
        }
    }
}

# ---------------------------------------------------------------------------
if (-not $SkipApp) {
    Write-Step 'Setting up the desktop app toolchain'

    $cargoBin = Join-Path $env:USERPROFILE '.cargo\bin'
    if (-not (Test-Tool 'cargo') -and -not (Test-Path (Join-Path $cargoBin 'cargo.exe'))) {
        Write-Host '      Installing Rust...' -ForegroundColor DarkGray
        if (Test-Tool 'winget') {
            & winget install --id Rustlang.Rustup --silent --accept-package-agreements --accept-source-agreements
        }
        else {
            throw "Rust is required for the desktop app. Install it from https://rustup.rs, or re-run with -SkipApp."
        }
    }
    if (Test-Path $cargoBin) { $env:PATH = "$cargoBin;$env:PATH" }
    if (Test-Tool 'cargo') { Write-Ok ((& cargo --version)) }

    if (-not (Test-Tool 'node')) {
        Write-Note 'Node.js is not installed. Install it (winget install OpenJS.NodeJS.LTS), then re-run.'
    }
    else {
        Write-Ok ("Node $(& node --version)")
        $appPkg = Join-Path $RepoRoot 'app\package.json'
        if (Test-Path $appPkg) {
            Write-Host '      Installing frontend dependencies...' -ForegroundColor DarkGray
            Push-Location (Join-Path $RepoRoot 'app')
            try {
                & npm install --no-fund --no-audit
                if ($LASTEXITCODE -ne 0) { throw "npm install failed (exit $LASTEXITCODE)." }
                Write-Ok 'Frontend dependencies installed.'
            }
            finally { Pop-Location }
        }
        else { Write-Skip 'App not scaffolded yet; skipping npm install.' }
    }
}

# ---------------------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Step 'Building the training engine'

    $buildDir = Join-Path $EngineDir 'build'
    $generator = if (Test-Tool 'ninja') { @('-G', 'Ninja Multi-Config') } else { @() }

    Push-Location $EngineDir
    try {
        & cmake -S . -B build @generator "-DCMAKE_PREFIX_PATH=$torchDir"
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)." }

        # Release only: on Windows, Debug and Release libtorch are ABI-incompatible.
        & cmake --build build --config Release --parallel
        if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
    }
    finally { Pop-Location }

    $exe = Get-ChildItem -Path $buildDir -Filter 'rl-engine.exe' -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($exe) { Write-Ok "Built $($exe.FullName)" }
    else { Write-Note 'Build reported success but rl-engine.exe was not found.' }
}

# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '  Setup complete.' -ForegroundColor Green
Write-Host ''
Write-Host '  Next step: dump your arena collision meshes.' -ForegroundColor White
Write-Host '  RocketSim needs Rocket League''s arena geometry, which cannot be redistributed,' -ForegroundColor DarkGray
Write-Host '  so you dump it once from your own installation:' -ForegroundColor DarkGray
Write-Host ''
Write-Host '    1. Start Rocket League and sit in the main menu' -ForegroundColor DarkGray
Write-Host '    2. Run RLArenaCollisionDumper (https://github.com/ZealanL/RLArenaCollisionDumper)' -ForegroundColor DarkGray
Write-Host '    3. Copy its collision-meshes output into engine/assets/collision_meshes' -ForegroundColor DarkGray
Write-Host ''
Write-Host '  Then run  pwsh -File tools/check-env.ps1  to confirm everything is ready.' -ForegroundColor DarkGray
Write-Host ''
