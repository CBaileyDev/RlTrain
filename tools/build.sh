#!/usr/bin/env bash
# Builds the RL Studio training engine on Linux. Counterpart of tools/build.ps1.
#
#   bash tools/build.sh                 # Release build
#   bash tools/build.sh --test          # build, then run the unit tests
#   bash tools/build.sh --clean         # delete engine/build first
#   bash tools/build.sh --target rl-engine
#   bash tools/build.sh --config RelWithDebInfo
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE_DIR="$REPO_ROOT/engine"
BUILD_DIR="$ENGINE_DIR/build"
TORCH_DIR="$ENGINE_DIR/libtorch"

CONFIG=Release; TARGET=""; CLEAN=0; TEST=0; RECONFIGURE=0
while (($#)); do
    case "$1" in
        --config) CONFIG="$2"; shift ;;
        --target) TARGET="$2"; shift ;;
        --clean) CLEAN=1 ;;
        --test) TEST=1 ;;
        --reconfigure) RECONFIGURE=1 ;;
        -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

[[ -f "$TORCH_DIR/share/cmake/Torch/TorchConfig.cmake" ]] || { echo 'libtorch missing. Run: bash tools/setup.sh' >&2; exit 1; }
[[ -f "$ENGINE_DIR/third_party/RocketSim/src/RocketSim.h" ]] || { echo 'RocketSim missing. Run: bash tools/setup.sh' >&2; exit 1; }

# Let CMake find nvcc when the CUDA toolkit is installed in the default location but not on PATH.
if ! command -v nvcc >/dev/null 2>&1 && [[ -x /usr/local/cuda/bin/nvcc ]]; then
    export PATH="/usr/local/cuda/bin:$PATH"
fi

if ((CLEAN)); then
    rm -rf "$BUILD_DIR"
fi

gen=()
command -v ninja >/dev/null 2>&1 && gen=(-G Ninja)
if ((RECONFIGURE)) || [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    cmake -S "$ENGINE_DIR" -B "$BUILD_DIR" "${gen[@]}" \
        -DCMAKE_BUILD_TYPE="$CONFIG" \
        -DCMAKE_PREFIX_PATH="$TORCH_DIR"
fi

jobs="$(nproc 2>/dev/null || echo 4)"
if [[ -n "$TARGET" ]]; then
    cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$jobs" --target "$TARGET"
else
    cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel "$jobs"
fi

if ((TEST)); then
    ctest --test-dir "$BUILD_DIR" -C "$CONFIG" --output-on-failure
fi

exe="$BUILD_DIR/bin/rl-engine"
if [[ -x "$exe" ]]; then
    printf '\033[32mBuilt %s\033[0m\n' "$exe"
elif [[ -z "$TARGET" ]]; then
    echo 'Build reported success but bin/rl-engine was not found.' >&2
    exit 1
fi
