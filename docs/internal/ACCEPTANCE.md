# Acceptance checks for RL Studio v0.1

The software workbench is ready when a user can configure a local run, train a policy,
observe actual metrics/frames, change reward weights, stop and save, resume, and compare
saved policies without game assets. The UI must explain its settings, handle missing
resources, persist preferences, and offer a usable learning handbook.

| Area | Evidence |
| --- | --- |
| Physics and environment | Catch2 arena, action, observation, symmetry and reward tests |
| PPO numerics | Hand-computed GAE goal/truncation cases; finite integration metrics |
| Actual learning updates | Model archive changes across optimizer updates |
| Persistence | Saved optimizer/model reload, including CUDA-to-CPU |
| Control lifecycle | Pause, live reward acknowledgement, resume, graceful stop |
| UI | Type/accessibility checks and browser interaction/layout regression |
| Desktop build | Optimized Tauri executable with embedded frontend |
| AI | Vault integration and proposal validation compiled; live account call untested |

Passing these checks does not establish ranked strength, convergence or exact reproducibility.
Native UI automation was unavailable under the environment's approval policy. That
validation limit remains explicit rather than being treated as a passing test.
