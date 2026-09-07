# Getting started

This walks you from a fresh clone to watching a bot learn. Budget about an hour, most of which is
downloading and compiling while you do something else.

---

## 1. Install the prerequisites

You need three things before anything else works.

**Visual Studio with C++ support.** The engine is C++ and needs Microsoft's compiler.

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

If you already have Visual Studio, open the Visual Studio Installer and make sure
**Desktop development with C++** is checked.

**CMake, Ninja and Git.**

```powershell
winget install Kitware.CMake Ninja-build.Ninja Git.Git
```

**The CUDA Toolkit**, if you have an NVIDIA GPU and want training to be fast.

```powershell
winget install --id Nvidia.CUDA --version 13.0
```

You need this even though the libraries we download already contain the CUDA code they run. The
build system checks for a real toolkit while configuring. If you skip it, run setup with `-Cpu`
later and everything still works, just far more slowly.

> **Why version 13.0 specifically?** It has to match the build of libtorch that setup downloads.
> Mixing versions produces confusing link errors.

---

## 2. Run setup

From the repository root:

```powershell
pwsh -File tools/setup.ps1
```

This fetches RocketSim, downloads libtorch (about 3 GB, the slow part), installs the app's
dependencies, and compiles the engine. It is safe to interrupt and re-run; each step skips work
already done.

Then confirm your machine is ready:

```powershell
pwsh -File tools/check-env.ps1
```

Every line is either `OK`, a `WARN` about an optional feature, or a `FAIL` with the exact command
that fixes it. Work through any failures before continuing. If something goes wrong,
[Troubleshooting](./troubleshooting.md) has the specific errors and their causes.

---

## 3. Choose an arena

RocketSim reproduces Rocket League's physics, but it needs the arena's collision geometry: the
floor, the curved walls, the corner ramps, the goal mouths. That geometry is part of the game and
cannot be redistributed, so you have two options.

### Practice Arena — works immediately

RL Studio generates a simplified arena from published field dimensions. Flat floor, flat walls,
a ceiling, and goal openings in the right places.

```powershell
rl-engine make-practice-arena
```

Physics, boost pads, goals and demolitions all work. Everything you learn about reinforcement
learning here transfers exactly. What you lose is fidelity: the real arena has curved walls and
corner ramps that shape how the ball bounces and how good players move, so a bot trained only on
the Practice Arena will not transfer perfectly to the real game.

Start here. You can switch later without losing anything.

### Accurate Arena — matches the real game

Dump the real geometry from your own installation. You only ever do this once.

1. Start Rocket League and sit at the main menu.
2. Download [RLArenaCollisionDumper](https://github.com/ZealanL/RLArenaCollisionDumper) and run it.
   It reads the arena geometry out of the running game and writes a `collision-meshes` folder.
3. Copy that folder's contents into `engine/assets/collision_meshes` in this repository.

Then check it took:

```powershell
pwsh -File tools/check-env.ps1
```

The Collision meshes line should turn `OK`.

> Nothing you dump is uploaded anywhere, and `.gitignore` already excludes it so you cannot
> accidentally commit it.

---

## 4. Run your first training

Try the engine on its own first. It reports how fast your machine can simulate:

```powershell
rl-engine bench
```

Then start training with the beginner preset:

```powershell
rl-engine train --config configs/presets/1v1-basics.json
```

You should see reward numbers rising within a few minutes. That preset uses dense shaping rewards
that pay the bot for driving toward the ball and touching it, which gives a signal early. Scoring
goals comes later.

Stop it with Ctrl+C at any point. Progress is saved to `runs/`.

---

## 5. Open the app

```powershell
cd app
npm run tauri dev
```

The onboarding wizard runs on first launch and re-checks everything above, so if you skipped ahead
it will tell you what is missing.

Then work through the app in this order:

1. **Train** — pick a preset and press Start. Every setting has an explanation next to it.
2. **Dashboard** — watch the graphs. [Reading the graphs](./concepts/reading-the-graphs.md)
   explains what each one means and what to do when one looks wrong.
3. **Match** — watch your bot play. This is the fastest way to understand what your reward function
   is actually encouraging, which is rarely exactly what you intended.
4. **Rewards** — change a weight while training is running and watch the effect land on the
   dashboard as a marked event.

---

## 6. Learn what you are looking at

The documentation is written to be read in order, and the same pages are available inside the app
with every setting linking to its own explanation.

If you want to understand the whole system, start at
[What is reinforcement learning](./concepts/what-is-rl.md) and read forward.

If you want to start experimenting today, read these three:

- [Rewards](./concepts/rewards.md) — the thing you will actually be changing
- [Reading the graphs](./concepts/reading-the-graphs.md) — how to tell whether it is working
- [Exploration and entropy](./concepts/exploration.md) — the most common way training quietly fails

---

## What to try next

- Train the `1v1-basics` preset until the bot reliably chases and hits the ball, then switch to
  `1v1-scoring` and watch what changes.
- Change one reward weight at a time and compare runs in the **Runs** screen. Changing several at
  once teaches you nothing about which one mattered.
- Use **Eval** in the Match screen to play an early checkpoint against a later one. Watching a bot
  beat its past self is the clearest possible evidence that learning happened.
- Ask the Assistant why a metric looks the way it does. It reads your live metrics and configuration
  and can propose specific changes, which you approve before anything is applied.
