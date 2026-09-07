# Settings reference

The schema in configs/schema/run.schema.json is authoritative. Both engine and UI
use the same defaults, bounds and explanations.

## Cross-field rules

- actionDelayTicks must be smaller than tickSkip.
- Minibatch size cannot exceed arenas × teamSize × 2 × rolloutSteps.
- Resuming restores model and Adam state into fresh episodes.
- iterations means additional updates when resuming.
- Train edits affect the next run; only reward weights can change live.

| Setting | Default | Range / choices | Meaning |
| --- | --- | --- | --- |
| `name` | `First touch` | text | Name shown in the run library. |
| `arena` | `practice` | practice, accurate | Practice uses generated geometry; accurate needs dumped meshes. |
| `meshPath` | `engine/assets/collision_meshes` | text | Directory containing accurate soccar meshes. |
| `device` | `auto` | auto, cpu, cuda | Auto selects CUDA when available. |
| `observation` | `advanced_v1` | advanced_v1, basic_v1 | Advanced includes opponents and boost pads; basic only sees self and ball. |
| `teamSize` | `1` | 1–4 | Cars per team. |
| `arenas` | `32` | 1–1024 | Parallel simulated matches. |
| `threads` | `8` | 1–32 | CPU workers stepping arenas. |
| `tickSkip` | `8` | 1–32 | Physics ticks per action at 120 Hz. |
| `actionDelayTicks` | `7` | 0–31 | Ticks using the previous action before applying a new action. |
| `rolloutSteps` | `128` | 8–4096 | Decisions per arena before each PPO update. |
| `iterations` | `100` | 1–1000000 | Number of PPO updates for this run. |
| `epochs` | `4` | 1–32 | Passes over each rollout. |
| `minibatchSize` | `1024` | 8–65536 | Samples per optimizer update. |
| `hiddenSize` | `256` | 32–1024 | Width of two neural network layers. |
| `seed` | `42` | 0–2147483647 | Random seed for policy and arena resets. |
| `checkpointEvery` | `10` | 1–10000 | Save a model every this many iterations. |
| `learningRate` | `0.00015` | 1e-07–0.01 | Adam learning rate. |
| `gamma` | `0.99` | 0–1 | Discount applied per decision. |
| `gaeLambda` | `0.95` | 0–1 | GAE bias versus variance tradeoff. |
| `clipRange` | `0.2` | 0.01–0.5 | Maximum policy ratio movement before clipping. |
| `entropyCoef` | `0.03` | 0–1 | Bonus for exploring actions. |
| `valueCoef` | `0.5` | 0–10 | Weight of value prediction error. |
| `maxGradNorm` | `0.5` | 0.01–10 | Gradient norm limit. |
| `episodeSeconds` | `60` | 1–600 | Time-limit truncation; bootstraps the final value. |
| `noTouchSeconds` | `30` | 1–600 | Reset after this long without a touch. |
| `velocityToBall` | `4` | -200–200 | Dense reward per second for moving toward the ball. |
| `faceBall` | `0.25` | -200–200 | Dense reward per second for facing the ball. |
| `ballToGoal` | `2` | -200–200 | Signed ball velocity toward the opponent goal. |
| `saveBoost` | `0.2` | -200–200 | Dense reward per second for retaining boost. |
| `airTime` | `0` | -200–200 | Dense reward per second while airborne. |
| `touch` | `5` | -200–200 | Reward on a ball touch. |
| `boostPickup` | `10` | -200–200 | Reward per full boost tank collected. |
| `goal` | `150` | -1000–1000 | Reward for scoring, negative for conceding. |
