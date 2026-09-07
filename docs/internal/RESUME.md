# Development status

The local RL Studio v0.1 workbench is implemented. The portable output is
`artifacts/RLStudio/rl-studio.exe`; build it with `tools/package.ps1`.

## Implemented

- RocketSim adapter, generated Practice Arena and 1v1 through 4v4 environments.
- 90 discrete actions; basic and advanced observations; time-aware reward terms.
- CPU/CUDA PPO, GAE, OpenMP stepping, checkpoint/Adam persistence and resume.
- Goal termination versus time-limit truncation, with endpoint value bootstrapping.
- Pause, resume, reward acknowledgements, graceful stop and window-close supervision.
- Standalone train/play/eval/bench commands and JSON-line control/event protocol.
- Tauri native process bridge, Svelte workbench, Three.js match view, uPlot metrics.
- Config import/export/persistence, three themes, onboarding, run library and comparisons.
- Optional OpenAI proposal assistant with OS credential storage and offline diagnostics.
- In-app handbook, current schema-backed references, build/start/package/test scripts.

## Validation

- Release C++ build: 18 Catch2 tests pass.
- `python -X utf8 tools/test-engine.py --cuda`: CPU and CUDA optimization, changed model
  archives, finite metrics, resume, evaluation, GPU checkpoint playback on CPU,
  GPU-to-CPU resumed training, invalid arguments, pause/reward/resume/stop all pass.
- A normal 32-arena CUDA configuration ran 8 updates / 65,536 agent steps with
  finite metrics. After warmup, measured throughput was roughly 31k–42k agent steps/s
  on this machine. This is a pipeline/performance check, not evidence of strong play.
- `npm --prefix app run check`: zero errors and zero warnings.
- `cargo test --manifest-path app/src-tauri/Cargo.toml`: native schema validation passes.
- Browser regression script passes navigation, editing, diagnostic labeling, Markdown,
  theme persistence, empty/playback states and 1100×700 layout.
- Optimized Windows desktop binary builds with an embedded frontend.

## Known scope and validation limits

No pretrained competitive policy is supplied. Strong play requires substantial training
and independent evaluation. Historical opponent pools, skill ratings, external RLViser
streaming and exact RNG/physics checkpoint continuation are not implemented.
The existing concept essays discuss some broader ideas; current behavior is documented
in how-it-works.md and the settings/rewards references.

The AI network request requires the user's API key/model and has not been exercised with
a live paid account. It validates returned numeric proposals before user review.
Native WebView end-to-end automation was blocked by automatic approval review when
launching with a remote debugging port; browser checks and direct engine checks are
separate evidence and must not be described as a native UI end-to-end test.

## Architecture decisions superseding older notes

The app uses supervised JSON lines over redirected process pipes, not the planned
WebSocket. Practice geometry uses a separate practice_meshes directory, avoiding
collision overlap with accurate meshes. Rust is now installed. Never re-run the old
cached workflow references from the original paused session; those were not used here.

Retain the CUDA/MSVC toolchain fixes in GROUNDING.md. Always build the engine through
tools/build.ps1, with the CUDA-compatible MSVC 14.4x toolset.
