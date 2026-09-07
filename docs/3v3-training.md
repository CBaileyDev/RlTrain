# Training a 3v3 policy

In Train, choose **Load 3v3 baseline**, then start a new run. Existing 1v1 checkpoints are incompatible with the six-car observation shape. Resume subsequent 3v3 runs using Runs > Resume latest.

This is an experimental starting recipe, not a proven optimal or competition-ready policy. It uses accurate geometry, CUDA, 32 arenas, six cars per arena, advanced observations, a 512-wide network, and 98,304 samples per PPO update. Three optimization epochs with 8,192-sample minibatches keep each collected batch from being reused excessively. Gamma 0.995 at 15 decisions per second gives rewards a discount half-life of about 9.2 seconds.

The default session is 1,018 updates (100,073,472 additional agent steps). Change the step target before starting. Stop saves a checkpoint; this budget is an evaluation interval, not a promise of competitive strength. Checkpoints save every 50 updates and on completion.

Movement and touch shaping are reduced relative to the original 1v1 recipe. Goal rewards are shared by all teammates and penalize conceding; boost collection, facing the ball, and passive airborne rewards are disabled. These weights still require evaluation: all players can chase the ball, and touches can be farmed. Watch for lost jumping, repeated weak touches, poor spacing, and ignored small boost pads.

Compare saved 3v3 models through Match evaluation and inspect both sides. Current evaluation is a diagnostic, not a tournament rating. Current training uses the same live policy for every car, kickoff resets, and no historical opponent pool or replay-state curriculum. Those are remaining development needs for a serious competitive pipeline. RL Studio currently runs the policy in RocketSim; deployment to RLBot is separate work.

Reward and evaluation guidance: https://github.com/ZealanL/RLGym-PPO-Guide/blob/main/making_a_good_bot.md . The numeric preset here is a project-specific hypothesis, not a configuration validated by that guide.
