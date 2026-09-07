# RL Studio

A desktop workbench for learning reinforcement learning by training Rocket League bots.

RL Studio trains a neural network to play Rocket League inside a headless physics simulation,
then shows you what it is learning: live training curves, a 3D view of the bot playing itself,
a reward editor you can adjust mid-run, and an assistant that reads your metrics and suggests
changes. Everything runs on your own machine.

> **What this is for.** This is a learning project. The bots it produces play in a simulator and
> in RL Studio's own viewer. It has no ability to interact with a live Rocket League match and
> is not a cheating tool.

---

## What is inside

| Piece | What it does |
|---|---|
| `rl-engine` | A C++20 trainer. Runs hundreds of Rocket League physics simulations in parallel and trains a policy with PPO on your GPU. Usable on its own from the command line. |
| RL Studio app | A desktop app that drives the engine, draws the training graphs, renders the 3D match view, and hosts the reward editor, docs and assistant. |

The engine and the app are separate programs that talk over a local WebSocket. You can use the
engine without the app, which is useful when you want to leave a long run going or script an experiment.

## Requirements

- Windows 10 or 11, 64-bit
- Visual Studio with the "Desktop development with C++" workload
- An NVIDIA GPU is strongly recommended. Training works on CPU but is dramatically slower.
- About 20 GB of free disk space, mostly libtorch
- Rocket League, but only once, and only to dump arena geometry. See below.

## Getting started

```powershell
pwsh -File tools/setup.ps1
```

That fetches every dependency, including a roughly 3 GB download of libtorch, and builds the engine.
It is safe to re-run; each step skips work that is already done.

Then check your machine:

```powershell
pwsh -File tools/check-env.ps1
```

Every line is either OK, a warning about an optional feature, or a failure with the exact command
that fixes it.

### About the arena

RocketSim needs Rocket League's arena geometry to simulate collisions accurately. That geometry is
part of the game and cannot be redistributed, so RL Studio gives you two options.

**Practice Arena.** Works immediately, no setup. RL Studio generates a simplified arena from
published field dimensions: a floor, a ceiling, four walls and goal openings. Physics, boost, goals
and demolitions all work. It is close enough that everything you learn about reinforcement learning
transfers, and it is what the onboarding wizard starts you on.

**Accurate Arena.** Matches the real game, including the curved walls and corner ramps that shape
real Rocket League play. You dump it once from your own installation:

1. Start Rocket League and sit at the main menu.
2. Run [RLArenaCollisionDumper](https://github.com/ZealanL/RLArenaCollisionDumper).
3. Copy its `collision-meshes` output into `engine/assets/collision_meshes`.

The app's onboarding wizard walks through this with screenshots. Nothing you dump is ever uploaded
or committed.

## Using the engine on its own

```bash
rl-engine train --config configs/presets/1v1-basics.json
rl-engine play --checkpoint runs/<run-id>/checkpoints/<step>
rl-engine eval --a <checkpointA> --b <checkpointB> --matches 20
rl-engine bench
```

`bench` is worth running first. It reports how many physics ticks and agent steps per second your
machine sustains, which tells you what settings are realistic.

## Documentation

The `docs/` folder is written to be read start to finish, and the same pages are browsable inside
the app with every setting linking to its own explanation.

- `docs/getting-started.md` — first run, start to finish
- `docs/how-it-works.md` — what actually happens during training
- `docs/concepts/` — the reinforcement learning ideas: policies and value functions, PPO, GAE,
  entropy, reward shaping, self-play, and what each training graph is telling you
- `docs/settings-reference.md` — every configuration field and what changing it does
- `docs/rewards-reference.md` — every reward term and the behavior it encourages
- `docs/troubleshooting.md` — when something will not build or the bot will not learn

## Repository layout

```
engine/     C++ training engine
  src/
    sim/        arena pool, game state snapshots, threading
    env/        observations, rewards, actions, episode resets, termination
    ppo/        networks, rollout buffer, GAE, the PPO update
    learner/    the training loop, checkpoints, metrics, skill rating
    ipc/        WebSocket server, RLViser streaming
    cli/        command-line entry point
  tests/      unit tests
app/        Tauri desktop app (Rust backend, Svelte frontend)
configs/
  schema/     JSON Schema for run configuration, the single source of truth
  presets/    ready-made configurations to start from
docs/       documentation, including the in-app pages
tools/      setup, build and diagnostic scripts
runs/       your training runs (not committed)
```

## Credits and licensing

RL Studio is MIT licensed.

It is built on [RocketSim](https://github.com/ZealanL/RocketSim) by ZealanL, an MIT-licensed
reimplementation of Rocket League's physics, and optionally uses
[RLViser](https://github.com/VirxEC/rlviser) by VirxEC for high-fidelity visualization.
The observation, action and reward designs follow conventions established by the
[RLGym](https://rlgym.org) community.

RL Studio is not affiliated with, endorsed by, or connected to Psyonix or Rocket League.
