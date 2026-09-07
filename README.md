# RL Studio

A local desktop workbench for learning reinforcement learning by training Rocket League
policies in RocketSim. Configure an experiment, watch simulated play, inspect actual PPO
metrics, edit rewards, and save or compare checkpoints.

## Run the app

The ready-to-run portable build is in `artifacts/RLStudio`. Open `rl-studio.exe` there.
Keep the folder together: the engine and its PyTorch runtime live alongside the app.
Windows 10/11 x64, WebView2, and the Visual C++ 2015–2022 runtime are required.
CUDA training additionally needs a compatible NVIDIA driver. The CUDA toolkit is needed
for building this source tree, not for running the packaged app.

From this source checkout, build and launch with:

```powershell
pwsh -File tools/build.ps1 -Test
pwsh -File tools/build-app.ps1
pwsh -File tools/start.ps1
```

For a fresh machine, `tools/setup.ps1` fetches the engine dependencies, and
`tools/check-env.ps1` diagnoses the toolchain. Building the desktop shell also requires
Rust and Node.js. See [Getting started](docs/getting-started.md).

## What works

- C++20 RocketSim environments, 1v1 through 4v4, OpenMP arena stepping.
- A versioned 90-action vocabulary; basic and opponent-aware normalized observations.
- Actual categorical PPO with GAE, gradient clipping, entropy regularization, CPU/CUDA.
- Time-limit bootstrapping distinct from terminal goals; configurable action delay.
- Live reward editing, pause/resume, graceful stop with checkpoint saving.
- Model and Adam checkpoint save/load; resumed training starts fresh simulator episodes.
- Two-model playback and first-goal/time-limit evaluation, plus a physics benchmark.
- Tauri/Svelte desktop app: onboarding, three themes, custom window controls, live charts,
  Three.js match view, run library, comparison charts, reward editor, configuration import/export.
- NeoToken tuning with an automatically generated telemetry prompt and optional note;
  bounded reward changes can apply live and learning settings are staged for the next run.
  Credentials are read natively from OpenCode; optional periodic tuning and an offline
  diagnostic are included. See [model selection and cost research](docs/assistant-models.md).
- Third-person car chase, ball tracking, a detailed procedural stadium, and fullscreen viewing.
- A Graphs workspace with raw traces, adjustable EMA smoothing, zoom and run overlays.
- Checkpoint resume directly on Train, with immediate restored and session step counts.
- Relative checkpoint Elo performance from evaluation matches; ranked MMR stays uncalibrated.
- In-app learning handbook and a schema-backed explanation for every configuration field.

This is a working learning tool, not a pretrained competitive bot. A short smoke test
validates the training pipeline, not policy quality. Strong play requires substantial
training, independent evaluation, and likely further algorithm/reward experimentation.
There is no integration with live Rocket League matches.

## Choose your arena

**Practice Arena** is generated automatically and contains no game assets. It approximates
field dimensions, walls, corners and goals. Files are kept in `engine/assets/practice_meshes`.
The viewer draws a simplified field; it is not a collision-mesh inspection tool.

**Accurate Arena** uses meshes dumped from your own installation with
[RLArenaCollisionDumper](https://github.com/ZealanL/RLArenaCollisionDumper). Select Accurate
Arena and enter the folder containing `soccar/*.cmf`. Keep accurate meshes separate from
practice meshes. No dumped geometry is redistributed or uploaded.

## Standalone engine

Run from the repository/portable folder root:

```powershell
engine/build/bin/rl-engine.exe train --config configs/presets/1v1-basics.json
engine/build/bin/rl-engine.exe train --config configs/presets/smoke.json
engine/build/bin/rl-engine.exe train --config configs/presets/1v1-basics.json --checkpoint runs/<run>/checkpoints/<iteration>
engine/build/bin/rl-engine.exe play --checkpoint runs/<run>/checkpoints/<iteration>
engine/build/bin/rl-engine.exe eval --a <checkpoint-directory> --b <checkpoint-directory> --matches 20
engine/build/bin/rl-engine.exe bench
```

`--run <new-directory>` selects a training destination. Existing run directories are
never overwritten. `--interactive` accepts JSON controls on stdin; events are JSON lines
on stdout and diagnostics go to stderr. See [IPC protocol](docs/internal/IPC_PROTOCOL.md).
The desktop app supervises these pipes through Tauri rather than exposing a local server.
The vendored RLViser schemas remain available for future integration; external RLViser
streaming is not included in this release.

## Development and checks

```powershell
pwsh -File tools/build.ps1 -Test
python tools/test-engine.py
npm --prefix app run check
cargo test --manifest-path app/src-tauri/Cargo.toml
pwsh -File tools/package.ps1
```

The UI browser preview is `npm --prefix app run dev`. Native operations are explicitly
disabled there. Browser regression instructions are in `tools/test-ui.md`.

## Layout

| Directory | Purpose |
| --- | --- |
| `engine/src/sim` | Practice geometry and RocketSim snapshot adapter |
| `engine/src/env` | Actions, observations and reward calculation |
| `engine/src/ppo` | Advantage calculation |
| `engine/src/learner` | Configuration, PPO trainer, checkpoints, playback/evaluation |
| `engine/src/cli` | Command-line parser and control inbox |
| `app` | Svelte frontend and Rust process supervisor |
| `configs/schema` | Authoritative configuration schema |
| `configs/presets` | Baseline and short validation run |
| `docs` | Handbook, references and implementation notes |
| `runs` | Local metrics and models; not committed |

## Credits

MIT licensed. Built with RocketSim (ZealanL), PyTorch, Catch2, nlohmann/json,
FlatBuffers, Tauri, Svelte, Three.js, uPlot, marked and DOMPurify. See
[Third-party notices](THIRD_PARTY.md). No unlicensed reference-bot source was copied.
RL Studio is not affiliated with Psyonix or Rocket League.
