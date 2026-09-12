#!/usr/bin/env bash
# Launches RL Studio on Linux. Counterpart of start.bat.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

SOURCE_APP="$PWD/app/src-tauri/target/release/rl-studio"
SOURCE_ENGINE="$PWD/engine/build/bin/rl-engine"

if [[ -x "$SOURCE_APP" && -x "$SOURCE_ENGINE" ]]; then
    RL_STUDIO_HOME="$PWD" exec "$SOURCE_APP" "$@"
fi

echo 'RL Studio is not built yet.'
echo
[[ -x "$SOURCE_APP" ]]    || echo '  App:    bash tools/build-app.sh'
[[ -x "$SOURCE_ENGINE" ]] || echo '  Engine: bash tools/build.sh   (fresh machine: bash tools/setup.sh)'
exit 1
