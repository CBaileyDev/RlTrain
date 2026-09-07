# Where to pick up

Paused 2026-09-07 at the user's request, mid-way through generating the contract layer.

## State

Committed and working:

- Build toolchain. `pwsh -File tools/build.ps1` configures and builds RocketSim, libtorch with
  CUDA 13, Catch2 and FlatBuffers. Verified.
- `tools/check-env.ps1` reports every prerequisite. Everything passes on this machine except the
  collision meshes, which are optional now that the Practice Arena exists.
- Practice Arena generator and its tests, in `engine/src/sim/PracticeArena.*` and
  `engine/tests/TestPracticeArena.cpp`. Not yet compiled, because the contract layer it will sit
  alongside is incomplete.
- 11 concept documentation pages, plus getting-started and troubleshooting.
- Tauri app scaffold: builds, but has no design system or UI yet.

Uncommitted and INCOMPLETE. Three of roughly fifteen contract files were written before the pause:

```
engine/src/util/Common.h      287 lines
engine/src/sim/GameState.h    761 lines
engine/src/env/ObsBuilder.h   393 lines
```

They have not been compiled or reviewed. Treat them as a draft.

## Resuming

Three workflows were stopped mid-run. Each can resume from cache, so completed agents replay
instantly and only unfinished work re-runs.

```
Workflow({scriptPath: "<session>/workflows/scripts/rlstudio-foundation-contracts-wf_0acf2ce7-7fb.js",
          resumeFromRunId: "wf_0acf2ce7-7fb"})
```
Writes the remaining contract files, then runs four adversarial critics over the whole set.
Design and judging are already cached; the winning direction was EXTENSIBILITY, with 48 grafted
improvements from the other two designs.

```
Workflow({scriptPath: "<session>/workflows/scripts/rlstudio-app-shell-wf_34f9bd8c-308.js",
          resumeFromRunId: "wf_34f9bd8c-308"})
```
Picks a visual direction, then builds the design tokens, theme system, frameless window chrome,
navigation and error handling. The scaffold step is cached.

```
Workflow({scriptPath: "<session>/workflows/scripts/rlstudio-concept-docs-wf_a27bf219-176.js",
          resumeFromRunId: "wf_a27bf219-176"})
```
Only the index page and the cross-link and contradiction pass remain.

## After the contracts land

1. Build and fix compile errors across the contract headers plus PracticeArena.
2. Run the Practice Arena tests. They have never been executed.
3. Implement the engine: arena pool and threading, the built-in observation, reward, action,
   state-setter and terminal components, then the PPO learner, then the WebSocket server.
4. Wire the app to the engine over the protocol in `docs/internal/IPC_PROTOCOL.md` once it exists.

## Read first

`docs/internal/GROUNDING.md` holds every verified fact: the RocketSim API surface, pinned versions,
the RLViser wire format, and the three toolchain incompatibilities that took real debugging to find.
Do not re-derive them.
