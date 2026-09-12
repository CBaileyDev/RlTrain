# Getting started

## Open the workbench

Open `artifacts/RLStudio/rl-studio.exe` in the portable build. Keep the entire folder
in a writable location, such as your Desktop. Runs are saved in its `runs` folder.

From the source checkout, run `pwsh -File tools/build-app.ps1`, then
`pwsh -File tools/start.ps1`. The frontend is embedded in the release executable;
you do not need a development web server.

## Build from source

You need Windows x64, Visual Studio's Desktop development with C++ workload,
CMake, Ninja, Git, Rust and Node.js. GPU builds use CUDA Toolkit 13.0 and the
MSVC 14.4x toolset. `tools/build.ps1` imports the compatible compiler environment.

```powershell
pwsh -File tools/setup.ps1
pwsh -File tools/check-env.ps1
pwsh -File tools/build.ps1 -Test
pwsh -File tools/build-app.ps1
```

The CUDA build of libtorch needs a matching toolkit at configure time. For a CPU-only
build, use the setup script's `-Cpu` option. The [troubleshooting guide](troubleshooting.md)
explains build errors and runtime checks.

## Build on Linux (Fedora)

The engine and the desktop app both build on Linux. The Linux scripts mirror the
PowerShell ones and are untested outside Fedora KDE; report anything that breaks.

```bash
sudo dnf install gcc-c++ cmake ninja-build git curl unzip nodejs npm     webkit2gtk4.1-devel gtk3-devel libappindicator-gtk3-devel librsvg2-devel libxdo-devel openssl-devel
curl https://sh.rustup.rs -sSf | sh        # Rust, if you do not have it
bash tools/setup.sh                        # or: bash tools/setup.sh --cpu
bash tools/build.sh --test
bash tools/build-app.sh
./start.sh
```

GPU training needs the NVIDIA driver (580 or newer) and, to configure the CUDA build of
libtorch, the CUDA 13.0 toolkit from NVIDIA's Fedora repository. If CMake rejects your
GCC as too new for that toolkit, either install an older GCC and point CMake at it with
`-DCMAKE_CXX_COMPILER`, or build CPU-only. There is no Linux installer or portable folder
yet: run from the source checkout with `./start.sh`, which sets `RL_STUDIO_HOME` to the
checkout so the app finds `engine/build/bin/rl-engine`. Collision meshes still have to be
dumped on a machine that has Rocket League installed, then copied into
`engine/assets/collision_meshes`.

## Choose Practice Arena

The onboarding dialog lets you choose Practice or Accurate Arena. Start with Practice:
it generates approximate geometry automatically, without any Rocket League files.

Accurate Arena requires your own dumped `soccar/*.cmf` meshes. Enter the containing
folder in Train. Practice and accurate meshes must never share a folder: RocketSim
loads every mesh and overlapping hulls produce wrong collisions.

## Start a small experiment

In Train, keep the baseline settings for a first run. Auto device selects CUDA when
available. The batch size is arenas × cars per arena × rollout steps. Minibatch size
cannot exceed that batch.

Press **Start training**. The Overview screen shows real simulator frames and measured
reward, entropy, value loss and KL divergence. The first curves appear after one complete
rollout and PPO update. Empty charts before then are expected.

A policy starts randomly. A short run may produce no goals and few touches. Avoid
changing many settings at once. Use the [settings reference](settings-reference.md)
and [reward reference](rewards-reference.md) to plan a controlled experiment.

## Pause, edit, stop

Pause takes effect at a decision boundary. Resume continues the same rollout. Changes
in Train configure the next run. In Rewards, **Apply reward weights** sends live changes
to the engine; wait for its acknowledgement. Reward-scale changes make before/after
reward curves incomparable without accounting for the new recipe.

**Stop & save** discards a partially filled PPO rollout and saves the current model and
Adam state. Closing the app requests a graceful stop and waits for saving. If the engine does not
respond within 30 seconds, it is terminated; the last completed checkpoint remains.

## Replay and compare

Runs lists local experiments and checkpoints. **Watch latest** selects a model in Match.
Choose another compatible model for Orange, or leave it empty for self-play. **Evaluate**
runs without real-time pacing; each match ends at the first goal or the checkpoint's
episode time limit. Time limits are draws.

Resume latest starts a new run directory, restores model and optimizer state, and uses
fresh simulator episodes. It is not bit-for-bit continuation of an interrupted rollout.
The iterations field specifies additional updates for the resumed run.

## Optional assistant

In Settings, save your OpenAI API key and enter a model ID supported by your account.
Ask AI assistant sends your question, configuration and last 30 visible metric rows to
OpenAI. It proposes exact numeric changes for you to review. No changes apply merely
because a response arrived. API requests can incur charges. Local diagnostic works
without a key and is explicitly rule-based.
