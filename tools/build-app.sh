#!/usr/bin/env bash
# Builds the RL Studio desktop app (Tauri + Svelte) on Linux. Counterpart of tools/build-app.ps1.
#   bash tools/build-app.sh            # release
#   bash tools/build-app.sh --debug    # debug
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ -x "$HOME/.cargo/bin/cargo" ]] && export PATH="$HOME/.cargo/bin:$PATH"

npm --prefix "$REPO_ROOT/app" run check
npm --prefix "$REPO_ROOT/app" run build

args=(build --manifest-path "$REPO_ROOT/app/src-tauri/Cargo.toml" --features custom-protocol)
[[ "${1:-}" == "--debug" ]] || args+=(--release)
cargo "${args[@]}"
printf '\033[32mDesktop app built with embedded frontend.\033[0m\n'
