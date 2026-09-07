# Engine IPC protocol, version 1

The desktop supervisor launches rl-engine directly with redirected stdin/stdout/stderr.
It sets --interactive so EOF on stdin requests a graceful stop. No shell, listening port,
or WebSocket is involved. This is a deliberate simplification of the original plan.

## Framing

UTF-8 JSON, one object per line. Stdout contains only protocol objects. Stderr is human
logging. The Rust supervisor emits engine-event and engine-log Tauri events to Svelte.
Malformed input JSON is ignored; unknown controls have no effect. Keep control messages
small. The app uses a bounded 32-command queue.

## Commands

- `{"type":"pause"}`: pause at a decision boundary; acknowledge status paused.
- `{"type":"resume"}`: resume the same rollout; acknowledge status running.
- `{"type":"stop"}`: discard incomplete rollout, save current model/optimizer, exit.
- `{"type":"rewards","values":{"touch":7}}`: validate and apply reward fields only;
  acknowledge a config event containing the active configuration.

## Events

- started: run directory, actual device, config, observation width.
- metrics: iteration, steps, reward, policyLoss, valueLoss, entropy, kl, clipFraction,
  explainedVariance, stepsPerSecond, touches, goals, episodes, elapsedSeconds.
- frame: tick, ball xyz, cars (id/team/pos/forward/up/boost/demoed), cumulative score.
- checkpoint: checkpoint directory and iteration.
- config: acknowledged active configuration.
- status: running, paused, playing, completed or stopped.
- evaluation: completed matches, blueWins, orangeWins, draws.
- benchmark: physicsTicksPerSecond and agentStepsPerSecond for one arena.
- error: human-readable message. Fatal command failures also exit nonzero.

The supervisor adds exit with the process exit code. Errors use nonzero exit codes;
invalid CLI arguments normally use 2. Events are flushed immediately. Frame publication
is throttled to around 10 Hz during training and decision frequency during playback.
Checkpoint directories ending .pending must not be offered for loading.
