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
