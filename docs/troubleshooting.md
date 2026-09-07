# Troubleshooting

Start here:

```powershell
pwsh -File tools/check-env.ps1
```

It checks every prerequisite and prints the exact command to fix whatever is missing. Most problems
below are things it will catch for you.

---

## Build problems

### "Your installed Caffe2 version uses CUDA but I cannot find the CUDA libraries"

**Cause.** The CUDA build of libtorch needs a CUDA Toolkit installed to *compile against*, even
though libtorch ships every CUDA DLL it needs to *run*. Its CMake config calls `find_package(CUDA)`
while configuring, and that fails without a real toolkit.

**Fix.**

```powershell
winget install --id Nvidia.CUDA --version 13.0
```

Version 13.0 matches the `cu130` libtorch that `setup.ps1` downloads. If you would rather not
install several gigabytes of toolkit, rebuild against the CPU libtorch instead:

```powershell
pwsh -File tools/setup.ps1 -Cpu
```

Training will still work. It will be much slower.

### "Invalid character escape '\P'" during configure

**Cause.** A CUDA Toolkit path containing backslashes was passed to CMake. libtorch bundles an old
copy of the `FindCUDA` module that pastes the path into a CMake string, where `\P` in
`C:\Program Files\...` is read as an escape sequence.

**Fix.** Build through `tools/build.ps1`, which always passes forward slashes. If you are running
`cmake` by hand, write the path as `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.0`.

### "unsupported Microsoft Visual Studio version! Only the versions between 2019 and 2022"

**Cause.** `nvcc` refuses to work with a compiler newer than it knows about. If you have Visual
Studio 2026, its default toolset is too new for CUDA 13.

**Fix.** Build through `tools/build.ps1`. It detects this and selects an older 14.4x toolset
automatically. If it reports that no compatible toolset exists, open the Visual Studio Installer and
add **MSVC v143 - VS 2022 C++ x64/x86 build tools**.

Do not work around this with `-allow-unsupported-compiler`. It gets past the version check and then
fails worse, with `nvcc error : 'cudafe++' died with status 0xC0000005 (ACCESS_VIOLATION)`.

### "cudafe++ died with status 0xC0000005"

Same cause as above, one step further along. `nvcc` crashed parsing the newer standard library
headers. The fix is the same: use a 14.4x toolset, do not force the version check.

### "No CMAKE_CXX_COMPILER could be found"

**Cause.** CMake's Ninja generator needs `cl.exe` on `PATH` plus a set of `INCLUDE` and `LIB`
variables that only exist inside a Visual Studio developer shell.

**Fix.** Use `tools/build.ps1`, which imports that environment for you. Calling `cmake` directly
from an ordinary PowerShell prompt will not work.

### The build succeeds but `rl-engine.exe` fails to start

Almost always a missing libtorch DLL. The build copies them next to the executable automatically,
so check that `engine/build/bin/` (or `engine/build/Release/`) contains `torch_cpu.dll`,
`c10.dll` and, for CUDA builds, `torch_cuda.dll`. If they are missing, rebuild:

```powershell
pwsh -File tools/build.ps1 -Clean
```

### Linker errors mentioning `std::` types or iterator debug levels

You built in Debug against the Release libtorch. On Windows those are not ABI-compatible.
RL Studio only supports Release and RelWithDebInfo. Delete `engine/build` and rebuild.

---

## Simulation problems

### "RocketSim has not been initialized" or the ball falls through the floor

The arena has no collision geometry. RocketSim loads Rocket League's arena meshes, and without them
there is no floor, no walls and no goals.

**Fix.** Either generate the Practice Arena:

```powershell
rl-engine make-practice-arena
```

or dump the accurate geometry from your own copy of the game. See
[Getting started](./getting-started.md) for the walkthrough.

### "Collision mesh does not match any known soccar collision mesh"

A warning, not an error. It means a loaded mesh is not one of the sixteen hashes RocketSim
recognizes. Two normal reasons:

- You are using the Practice Arena, which is generated rather than dumped. Expected, ignore it.
- Your dump came from a modified or non-standard arena. Re-dump from a normal soccar match.

Training still runs either way.

### Training is far slower than expected

Run the benchmark first so you know what your machine can actually do:

```powershell
rl-engine bench
```

Then check, in order:

1. **Is it using the GPU?** The dashboard shows the device. If it says CPU, either your libtorch is
   the CPU build or no compatible GPU was found. `check-env.ps1` tells you which.
2. **Too many arenas.** More arenas is not always faster. Past the point where the simulation
   saturates your cores, you add memory pressure without adding throughput. Try halving
   `env.num_arenas`.
3. **Too few arenas.** The opposite failure. With too few, the GPU sits idle waiting for one small
   batch at a time. On a 16-core machine, a few hundred arenas is a reasonable starting point.
4. **Network too large.** A wider or deeper policy costs time on every single step.
5. **The 3D viewer is open.** Streaming state costs a little throughput. Close the Match view for
   maximum speed.

### Out of GPU memory

Lower `ppo.minibatch_size` first, then `env.num_arenas`, then the network width. Minibatch size has
the largest effect on peak memory and the smallest effect on learning.

---

## Training problems

These are not bugs. They are the normal ways reinforcement learning goes wrong, and reading the
graphs is a skill worth building. [Reading the graphs](./concepts/reading-the-graphs.md) covers each
in depth.

### Reward is flat and nothing is happening

Usually the reward is too sparse to give any signal. If the only reward is scoring a goal, a bot
that moves randomly will essentially never score, so there is nothing to learn from. Start from the
`1v1-basics` preset, which rewards driving toward the ball and touching it, then add scoring rewards
once the bot reliably hits the ball.

### Entropy collapsed to near zero early

The policy became deterministic before it learned anything, so it stopped exploring and is now stuck.
Raise `ppo.entropy_coefficient`, lower the learning rate, or both. See
[Exploration and entropy](./concepts/exploration.md).

### Reward climbs but the bot plays badly

Reward hacking: the bot found something that scores well under your reward function but is not the
behavior you wanted. A classic example is sitting next to the ball nudging it to farm a touch
reward. Watch it play in the Match view, work out what it is actually optimizing, and adjust the
weights. [Rewards](./concepts/rewards.md) covers this.

### Reward was improving and then collapsed

Usually too large a policy update. Lower the learning rate, lower `ppo.clip_range`, or reduce
`ppo.epochs`. Check whether approximate KL divergence spiked just before the collapse; that is the
signature.

### Explained variance is near zero or negative

The value network is not predicting returns any better than guessing the mean, which makes the
advantage estimates mostly noise. Give the critic more capacity or a higher learning rate, and check
that your rewards are not wildly different in scale from each other.

---

## App problems

### The window opens blank or white

The WebView2 runtime is missing or broken. It ships with Windows 11, but you can reinstall the
Evergreen WebView2 Runtime from Microsoft. `check-env.ps1` reports whether it was detected.

### "Engine disconnected" or the app cannot start training

The app launches `rl-engine.exe` as a child process. If it is missing or crashes on startup, the app
reports it. Confirm the engine works on its own:

```powershell
rl-engine bench
```

If that fails, the problem is the engine build, not the app. Work through the build section above.

The error dialog has a copy button; the full output including the exit code is under the technical
detail section.

### The assistant will not respond

It needs an API key, entered in Settings. The key is stored in Windows Credential Manager, never in
a file in this repository. If requests fail, check that the base URL and model name in Settings match
your provider, and that the model appears in the catalog the Settings page loads.

---

## Still stuck

1. `pwsh -File tools/check-env.ps1` and read every non-OK line.
2. Look in `runs/<run-id>/log.txt` for the run that failed.
3. Rebuild from scratch with `pwsh -File tools/build.ps1 -Clean`.
4. Verified facts about versions, paths and known toolchain incompatibilities are recorded in
   `docs/internal/GROUNDING.md`.
