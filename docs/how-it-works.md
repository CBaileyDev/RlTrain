# How RL Studio works

## From simulator to policy

The C++ engine owns a set of RocketSim arenas. Each arena has blue and orange cars,
a ball and boost pads. Stable car slots keep observations and actions in the same order.
OpenMP steps different arenas on separate CPU workers; each arena has exclusive ownership.

An observation builder encodes normalized ball and car physics. The advanced layout
includes boost availability, opponents, teammates and previous controller inputs.
Orange coordinates are rotated 180 degrees so one shared policy can play both sides.

The network has a two-layer feature trunk, a 90-way categorical policy head and a scalar
value head. The policy samples actions, then the simulator executes the previous action
for actionDelayTicks and the selected action for the rest of tickSkip.

## Rewards and episode boundaries

Dense rewards are rates multiplied by elapsed simulated seconds. Touches, boost increases
and goals are event signals. Goals terminate an episode. Episode and no-touch time limits
truncate it. The trainer evaluates the final observation before resetting an arena.
Goals use zero bootstrap value; truncations retain the endpoint value. Both cut the
recursive advantage trace so a new episode cannot leak into an old one.

## The PPO update

The engine stores a full time-major rollout: observations, actions, old log probabilities,
values, rewards, endpoint values and reset masks. GAE computes advantages backward in time.
Advantages are normalized over the rollout, while returns remain in reward units.

For each shuffled minibatch, PPO compares new and old action probabilities, clips the
surrogate objective, fits the value head and adds an entropy bonus. Gradients are clipped
before Adam updates the parameters. Non-finite loss stops the run with an error.

## Processes and files

The app is a Tauri WebView plus a Rust supervisor. Rust launches the engine directly,
without a shell, and exchanges newline-delimited JSON over redirected pipes. Tauri events
carry metrics and frames to Svelte. This implementation replaces the original WebSocket
plan: no listening port or extra local server is needed.

Run directories contain config.json, metrics.jsonl, summary.json and checkpoints. Each
checkpoint contains model.pt, optimizer.pt, config.json and metadata.json. Checkpoints
are first written to a .pending directory, then renamed into place. Incomplete checkpoint
directories do not appear in the library.

## What reproducibility means here

Seeds control network initialization and per-arena kickoff random streams. Parallel arena
ownership is stable, but CUDA kernels and cross-device arithmetic can differ. Checkpoint
resume restores model and Adam state, not physics states or the random generator's exact
position. It starts fresh episodes. Compare multiple seeds and independent evaluations
before attributing an improvement to a setting.

## Evaluation limits

Current self-play uses the same learning policy for both teams. There is no historical
opponent pool or automatic skill rating. Evaluation compares supplied compatible models
in first-goal/time-limit episodes. The built-in viewer is simplified Three.js geometry,
not a rendering of the actual collision mesh. No external RLViser connection is enabled.
