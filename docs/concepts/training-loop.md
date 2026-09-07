# One Training Iteration End to End

**Training is a loop of five phases that repeat forever: the bot plays with frozen weights until the rollout is full, the advantages are computed, PPO runs its update phase over that data, metrics are drawn, and checkpoints are written.**

## Why this matters

This page assumes you have read [What reinforcement learning is](./what-is-rl.md). It uses the vocabulary defined there — observation, action, reward, done, timestep, transition, episode — without redefining it.

When you press **Train** and the first point appears on the graph, between fifty and a hundred thousand simulated decisions have already happened. If you cannot say what happened in between, you cannot debug a run. You will change a setting, see nothing happen, and conclude the setting does nothing — when in fact it took effect three minutes later at the next iteration boundary.

Every other page in `docs/concepts/` explains one part of the machine. [What the bot sees](./observations.md) explains the input. [What the bot can do](./actions.md) explains the output. [Designing the reward](./rewards.md) explains the score. [PPO](./ppo.md) explains how the weights change. [GAE](./gae.md) explains how the advantages are estimated. None of those pages tell you the order the parts run in. That order is what this page is for. After it you should be able to point at any phase and name the page that owns it.

This page is about the algorithm's loop. The separate question of *which process runs what* — the `rl-engine` process, the desktop app, and the redirected JSON pipes between them — is covered in [How it works](../how-it-works.md).

## The five phases

One **iteration** is one full trip around this loop. It is the unit that everything else is measured in.

### 1. Collect

Every arena in the pool steps forward at the same time. For each arena, at each decision point, RL Studio builds an observation vector for every car and runs it through the policy network, which returns a probability for each of the 90 button combinations the bot is allowed to press. It rolls a weighted die against those probabilities to pick one, writes those controls into RocketSim, and steps the physics forward by `env.tick_skip` ticks. It then computes the reward and records whether the episode ended.

What gets stored for each decision is a transition, in the sense defined on [What reinforcement learning is](./what-is-rl.md), plus two extras the learner needs later. The first is the **log-probability** of the action: the natural log of the probability of the combination the bot happened to pick. PPO needs it in phase 3 to compare how likely the updated weights would have been to make that same choice. The second is the value network's estimate for that situation. These pile up in the rollout buffer until it holds `ppo.steps_per_iteration` agent steps.

The network's weights **do not change at all during this phase**. See [Policies, values and advantage](./policy-and-value.md) for what the two network outputs mean, and [Exploration and entropy](./exploration.md) for why the action is sampled rather than chosen greedily.

### 2. Estimate

The rollout buffer now holds raw rewards, not learning targets. Generalized Advantage Estimation walks backwards through each arena's trajectory and turns them into two things: an advantage for every action taken, and a value target for the value head.

An **advantage** is one number per decision saying how much better or worse that decision turned out than the bot expected from that situation. Positive means it beat expectations — it flicked the ball goalward from a position it usually fumbles. Negative means it fell short — it drove past the ball at full boost and gave up the possession. A **value target** is what the value head should have predicted for that situation, used to make its next prediction less wrong.

[GAE](./gae.md) owns this — including the important detail that a goal and a time limit have to be handled differently.

### 3. Update

PPO takes the whole batch and runs one **update phase** over it, adjusting the policy head and the value head. One update phase happens per iteration, but it is not one gradient step. Inside it there are `ppo.epochs` passes over the buffer, each pass split into minibatches of `ppo.minibatch_size`, and every minibatch produces its own gradient step. With the starting settings suggested on [PPO](./ppo.md) — two epochs and a minibatch about a quarter of the rollout — that is eight gradient steps in one iteration. This is the only moment in the loop when weights change, but they change many times inside it.

When the update finishes, the rollout buffer is **emptied**. PPO is on-policy: the transitions were produced by the old weights, and once the weights change those transitions no longer describe what the current bot would do. Nothing is kept between iterations except the weights themselves. This is why data collection is never reused — every iteration pays for a fresh rollout. If you have read about DQN or any other method with a replay buffer, this is the point where PPO differs from it.

[PPO](./ppo.md) explains the objective, the clipping, and what the epoch, minibatch, clip range and learning rate settings trade against each other.

### 4. Record

Metrics are computed from the batch just collected and the update just applied, then pushed to the dashboard. That is when a new point appears on every graph. In short:

- **mean reward** — how much score the bot collected per decision.
- **entropy** — how undecided the policy still is. High means it is still trying different things; the ceiling is ln(90) ≈ 4.50 for a policy that picks uniformly among the 90 actions.
- **explained variance** — how well the value head predicts outcomes, where 1.0 is perfect and 0.0 is no better than guessing the average.
- **KL divergence** — how far this update moved the policy away from the one that collected the data.
- **steps per second** — raw throughput.

[Reading the training graphs](./reading-the-graphs.md) gives each one a healthy range and tells you what to change when it goes wrong.

### 5. Maintain

Housekeeping on a schedule. A checkpoint is written every `run.checkpoint_interval` iterations. A self-play snapshot is added to the opponent pool on its own interval, described in [Self-play and skill rating](./self-play.md).

Then the loop returns to phase 1 with the new weights.

## Parallel arenas

PPO needs a large, varied batch. A gradient estimated from a hundred decisions is mostly noise; a gradient estimated from fifty thousand is a usable signal. One car produces 15 decisions per second of *simulated* game time, so a single 1v1 arena — two cars deciding at once — would have to simulate about 28 minutes of Rocket League to fill one 50,000-step rollout.

RocketSim is a headless physics simulator and runs much faster than real time, so that is not 28 minutes of waiting. But it is 28 minutes of game drawn from one continuous stretch of play, and that is the problem.

The fix is to run many independent arenas at once. `env.num_arenas` sets how many. The working range for this project is **256 or more**. They are genuinely independent: different kickoffs, different score states, different points in an episode.

The same number of samples drawn from 256 arenas is worth more than the same number drawn from one arena over a much longer stretch. Steps taken one after another in a single arena are highly correlated — the same rally, the same field position, the same boost situation, the same two cars chasing the ball down the same wall. A long single-arena batch therefore carries less independent information than its size suggests, and the gradient you compute from it has higher variance. And 256 arenas fill the buffer roughly 256 times sooner in wall clock, as long as you have the CPU threads to step them.

The honest limits: each arena costs memory and each one needs CPU time. RocketSim's `ArenaConfig` has a `memWeightMode` setting with two values. `LIGHT` uses about **383 KB** per arena with four cars; `HEAVY` uses about **1263 KB**. At 512 arenas that is roughly 192 MB versus 632 MB. `LIGHT` is the default for large pools for exactly that reason. On CPU side, `tools/check-env.ps1` warns you if your machine has fewer than 8 threads and tells you to lower `env.num_arenas` — with fewer threads the arenas queue up behind each other and the GPU sits idle waiting for data.

## Rollout size

`ppo.steps_per_iteration` is how many agent steps go into one batch. The working range here is **50,000 to 100,000**.

An agent step is one decision by one car. At 15 decisions per second, 50,000 agent steps is about 3,333 car-seconds. Car-seconds are not the same as seconds of match time: a 1v1 has two cars deciding at once, so 3,333 car-seconds is about 1,667 seconds of match time — roughly **28 minutes of simulated Rocket League per iteration**. 100,000 steps is about 56 minutes. In a 2v2 the same step count buys half as much game time again, because four cars are filling the buffer instead of two.

Spread across the pool, that is less alarming than it sounds. In a 1v1 with 256 arenas, 50,000 agent steps is 25,000 environment steps, so each arena advances about 98 steps — about 6.5 seconds of game time, or about 780 physics ticks at `tick_skip = 8`. Multiply back out and 256 arenas × 6.5 seconds is the same 1,667 seconds.

Both directions have a cost:

- **Bigger rollouts** average over more experience, so the gradient has lower variance and the update is more trustworthy. They also mean fewer updates per hour and slower feedback: you wait longer to find out whether a change helped.
- **Smaller rollouts** update more often, so you see the effect of a change sooner, but each gradient is noisier and the policy can lurch.

## The network

The shape is deliberately simple. A shared trunk of fully-connected layers, **256 to 1024 units wide**, each followed by a ReLU activation and LayerNorm. **ReLU** is the activation function between layers: it passes positive values through unchanged and replaces negatives with zero, which is what lets a stack of layers represent something other than a straight line.

The trunk splits into two heads. The first is a **policy head that outputs 90 numbers**, one per entry in RL Studio's fixed table of 90 button combinations — throttle, steer, pitch, yaw, roll, jump, boost and handbrake, quantized down to 90 combinations a real player would actually press ([What the bot can do](./actions.md) derives the number). Those 90 raw numbers are called **logits**: unnormalized scores, not probabilities. Running them through a softmax turns them into 90 probabilities that sum to 1, and the bot draws its action from that distribution. The second is a **value head that outputs one number**, its estimate of how the current situation will turn out.

`ppo.hidden_sizes` controls the trunk. Widening it adds capacity, and it also adds memory and makes every forward and backward pass slower. A 1024-wide layer feeding another 1024-wide layer holds 1024 × 1024 + 1024 ≈ 1.05 million parameters on its own. If your bottleneck is data collection rather than network capacity, a wider trunk buys you nothing and costs you throughput.

**LayerNorm** standardizes each layer's output vector one sample at a time: subtract the mean of that vector, divide by its standard deviation, then apply a learned scale and shift. It does not stop the network from learning that one feature matters more than another — the learned scale can do exactly that. What it does is keep the size of the activations stable from layer to layer and from iteration to iteration, which keeps the gradients well-behaved when the policy shifts.

That matters in this project because the policy shifts a lot. The bot that has just learned to hit the ball produces very different activations from the one that was driving in circles two hundred iterations ago, and you want the same learning rate to still be sensible after that change. Raw physical scale is a separate problem, handled on the input side — [What the bot sees](./observations.md) explains how positions in the thousands of unreal units, boost from 0 to 100 and angular velocity in radians per second all get normalized before they reach the trunk. [PPO](./ppo.md) explains what the two heads are used for.

## Where the work happens

Physics runs on **CPU threads**. Network forward and backward passes run on the **GPU**.

Say that plainly and a common surprise disappears: a fast GPU paired with a slow CPU can still be data-starved. The GPU finishes its update in seconds and then spends most of its time waiting on the arenas. A faster GPU still helps the inference that the collect phase depends on, but if physics is the constraint, most of what you paid for sits unused.

Note that the collect phase is not pure CPU work. It steps physics on CPU threads *and* runs a policy forward pass on the GPU for every car at every decision point. With a wide trunk and hundreds of arenas, that inference is not free.

`rl-engine bench` is how you find out which side you are limited by. It reports sustained throughput without training anything, so you can see whether the arenas or the network is the constraint.

## Throughput

Two numbers describe how fast a run is going:

- **Steps per second** — agent decisions per second summed across all arenas.
- **Ticks per second** — physics ticks per second. Each environment step advances `env.tick_skip` = 8 ticks, and in a 1v1 each environment step produces two agent steps, one per car. So ticks per second is about four times the steps-per-second readout above in a 1v1, and about twice it in a 2v2. If those two numbers look like they disagree, this is why.

Record both before and after you change anything performance-related. Three settings move them most: `env.num_arenas`, `env.tick_skip` (see [What the bot can do](./actions.md)) and `ppo.hidden_sizes`.

## Checkpoints and reproducibility

`run.checkpoint_interval` sets how often a checkpoint is written into the run folder. A checkpoint holds the network weights **and the spec needed to use them**: the observation spec, the action set, the tick skip, and the action delay — the number of physics ticks between the bot choosing an action and that action reaching the car, about 7 ticks, which stands in for the input lag a human player has. That bundle exists because a policy is meaningless without the exact input and output convention it was trained under. Load weights against a different observation builder and the bot will drive into a wall.

`run.seed` seeds the random number generators. It makes a run *similar*, not identical. Floating-point addition is not associative, and the GPU kernels PyTorch picks by default do not sum in a fixed order — nor does the order in which the worker threads write transitions into the rollout buffer. So two runs with the same seed drift apart, slowly at first and then completely.

This is a consequence of the fast code paths, not a bug. PyTorch can be forced into deterministic kernels, but it costs throughput and RL Studio does not do it by default. Treat the seed as a way to make experiments comparable, not as a guarantee of bit-exact reproduction.

## A misconception to correct

**"The bot is learning continuously while it plays."** It is not.

The bot plays for an entire rollout with **frozen weights**. Only after the rollout is full does any learning happen, and it happens in one burst in phase 3. Then it plays again, with the new weights, for another whole rollout.

Two things follow from this, and both of them will otherwise confuse you:

1. The graphs update in **steps, not smoothly** — one point per iteration, because there is only one update per iteration.
2. A setting you change **does not take effect until the next iteration boundary**. If you raise a reward weight mid-collect, the rest of that rollout is still being scored the old way.

## What to look for in RL Studio

- **The iteration counter.** It should tick up steadily. The time between increments is the length of one full loop.
- **The steps-per-second readout.** Note it when a run starts and compare it after any settings change. A sudden drop usually means you added arenas or widened the network past what your machine sustains.
- **The phase indicator.** It flips between *collect* and *update*. If it spends nearly all its time in collect, you are CPU-bound; run `rl-engine bench` to confirm.
- **The checkpoint list.** Open `runs/<run-id>/checkpoints/` and watch files appear on the `run.checkpoint_interval` schedule. If nothing is appearing, no iteration has completed yet.
