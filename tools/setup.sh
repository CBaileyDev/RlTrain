#!/usr/bin/env bash
# One-command setup for RL Studio on Linux (target: Fedora KDE).
# Linux counterpart of tools/setup.ps1. Safe to re-run; every step skips work already done.
#
#   bash tools/setup.sh                # CUDA libtorch (needs the CUDA 13.0 toolkit + NVIDIA driver 580+)
#   bash tools/setup.sh --cpu          # CPU-only libtorch (much smaller, much slower training)
#   bash tools/setup.sh --skip-app     # engine only, no Rust/Node
#   bash tools/setup.sh --skip-build   # fetch dependencies but do not compile
#
# What it deliberately does NOT do: dump Rocket League arena collision meshes. Those come
# from your own game install (see docs/getting-started.md).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE_DIR="$REPO_ROOT/engine"

# --- Pinned versions. Keep in sync with tools/setup.ps1 and docs/internal/GROUNDING.md. ---
LIBTORCH_VERSION='2.14.0'
LIBTORCH_CUDA='cu130'
ROCKETSIM_REPO='https://github.com/ZealanL/RocketSim.git'

CPU=0; SKIP_APP=0; SKIP_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --cpu) CPU=1 ;;
        --skip-app) SKIP_APP=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        -h|--help) sed -n '2,11p' "$0"; exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

step=0
say_step() { step=$((step + 1)); printf '\n  \033[36m[%d] %s\033[0m\n' "$step" "$1"; }
say_ok()   { printf '      \033[32m%s\033[0m\n' "$1"; }
say_skip() { printf '      \033[90m%s\033[0m\n' "$1"; }
say_note() { printf '      \033[33m%s\033[0m\n' "$1"; }
have()     { command -v "$1" >/dev/null 2>&1; }

printf '\n  RL Studio setup (Linux)\n  =======================\n  Repository: %s\n' "$REPO_ROOT"

# ---------------------------------------------------------------------------
say_step 'Checking the C++ toolchain'
missing=()
for t in g++ cmake git curl unzip; do have "$t" || missing+=("$t"); done
if ((${#missing[@]})); then
    echo "      Missing: ${missing[*]}" >&2
    say_note 'Fedora: sudo dnf install gcc-c++ cmake ninja-build git curl unzip'
    exit 1
fi
say_ok "$(g++ --version | head -1)"
say_ok "$(cmake --version | head -1)"
if have ninja; then
    say_ok 'Ninja found (fast builds enabled)'
else
    say_note 'Ninja not found; using Make (slower). Fedora: sudo dnf install ninja-build'
fi

# ---------------------------------------------------------------------------
say_step 'Fetching RocketSim (the Rocket League physics simulator)'
RS_DIR="$ENGINE_DIR/third_party/RocketSim"
if [[ -f "$RS_DIR/src/RocketSim.h" ]]; then
    say_skip 'Already present.'
else
    mkdir -p "$(dirname "$RS_DIR")"
    git clone --depth 1 "$ROCKETSIM_REPO" "$RS_DIR"
    rm -rf "$RS_DIR/.git"
    say_ok 'RocketSim vendored (MIT licensed).'
fi

# ---------------------------------------------------------------------------
say_step 'Checking for the CUDA Toolkit'
# The CUDA libtorch bundles its runtime .so files, but TorchConfig.cmake still needs a
# real toolkit (nvcc) installed to configure. Fedora: https://developer.nvidia.com/cuda-downloads
if ((CPU)); then
    say_skip 'Building CPU-only; the CUDA Toolkit is not needed.'
else
    CUDA_HOME="${CUDA_HOME:-/usr/local/cuda}"
    if have nvcc; then
        say_ok "$(nvcc --version | grep release)"
    elif [[ -x "$CUDA_HOME/bin/nvcc" ]]; then
        export PATH="$CUDA_HOME/bin:$PATH"
        say_ok "$(nvcc --version | grep release) (from $CUDA_HOME)"
    else
        say_note 'nvcc was not found. The CUDA build needs the CUDA 13.0 toolkit installed.'
        say_note '  Fedora: enable the NVIDIA CUDA repo, then: sudo dnf install cuda-toolkit-13-0'
        say_note '  Re-run this script afterwards, or re-run with --cpu to skip the GPU.'
        exit 1
    fi
    if have nvidia-smi; then
        say_ok "$(nvidia-smi --query-gpu=name,driver_version --format=csv,noheader | head -1)"
    else
        say_note 'nvidia-smi not found: the NVIDIA driver (580+) must be installed to train on the GPU.'
    fi
fi

# ---------------------------------------------------------------------------
say_step 'Downloading libtorch (the PyTorch C++ library)'
TORCH_DIR="$ENGINE_DIR/libtorch"
TORCH_CONFIG="$TORCH_DIR/share/cmake/Torch/TorchConfig.cmake"
if [[ -f "$TORCH_CONFIG" ]]; then
    say_skip 'Already present.'
else
    compute="$LIBTORCH_CUDA"
    ((CPU)) && compute=cpu
    DL_DIR="$ENGINE_DIR/.download"
    mkdir -p "$DL_DIR"
    ZIP="$DL_DIR/libtorch.zip"
    say_note 'Several GB for the CUDA build. This is the slow part; it only happens once.'
    ok=0
    # PyTorch has renamed the Linux archive over time; try the current name first.
    for name in \
        "libtorch-cxx11-abi-shared-with-deps-${LIBTORCH_VERSION}%2B${compute}.zip" \
        "libtorch-shared-with-deps-${LIBTORCH_VERSION}%2B${compute}.zip"; do
        url="https://download.pytorch.org/libtorch/${compute}/${name}"
        echo "      $url"
        if curl -L --fail --retry 3 --retry-delay 5 -C - -o "$ZIP" "$url"; then
            ok=1
            break
        fi
    done
    ((ok)) || { echo 'Download failed.' >&2; exit 1; }
    echo '      Extracting...'
    unzip -q -o "$ZIP" -d "$ENGINE_DIR"
    rm -f "$ZIP"
    [[ -f "$TORCH_CONFIG" ]] || { echo "Extraction finished but $TORCH_CONFIG is missing." >&2; exit 1; }
    say_ok "libtorch $LIBTORCH_VERSION ($compute) ready."
fi

# ---------------------------------------------------------------------------
if ((!SKIP_APP)); then
    say_step 'Setting up the desktop app toolchain'
    # Tauri 2 on Fedora needs WebKitGTK and friends to link.
    if have rpm && ! rpm -q webkit2gtk4.1-devel >/dev/null 2>&1; then
        say_note 'Tauri needs system libraries. Install them with:'
        say_note '  sudo dnf install webkit2gtk4.1-devel gtk3-devel libappindicator-gtk3-devel librsvg2-devel libxdo-devel openssl-devel'
    fi
    if ! have cargo && [[ -x "$HOME/.cargo/bin/cargo" ]]; then
        export PATH="$HOME/.cargo/bin:$PATH"
    fi
    if have cargo; then
        say_ok "$(cargo --version)"
    else
        say_note 'Rust is not installed: curl https://sh.rustup.rs -sSf | sh   (or re-run with --skip-app)'
    fi
    if have node; then
        say_ok "Node $(node --version)"
        (cd "$REPO_ROOT/app" && npm install --no-fund --no-audit)
        say_ok 'Frontend dependencies installed.'
    else
        say_note 'Node.js is not installed: sudo dnf install nodejs npm'
    fi
fi

# ---------------------------------------------------------------------------
if ((!SKIP_BUILD)); then
    say_step 'Building the training engine'
    bash "$REPO_ROOT/tools/build.sh"
fi

printf '\n  \033[32mSetup complete.\033[0m\n\n'
echo '  Next step: dump your arena collision meshes from your own Rocket League install'
echo '  (on a machine that has the game) and copy them into engine/assets/collision_meshes.'
echo '  See docs/getting-started.md. Then:  bash tools/build-app.sh && ./start.sh'
