# What the Bot Can Do

**An action in RL Studio is one complete Rocket League controller state — throttle, steer, pitch, yaw, roll, jump, boost, handbrake — chosen once every 8 physics ticks and held until the next decision.**

## Why this matters

You have already seen what the bot looks at ([What the Bot Sees](observations.md)) and what the policy network is ([Policies, Values and Advantage](policy-and-value.md)). This page covers the other end of that network: the thing it produces.

The action space is the single most under-appreciated setting in a Rocket League RL project. It decides what "trying something new" even means. If the action space is badly shaped, random exploration produces a car that vibrates in place, and the bot never stumbles into a first ball touch to be rewarded for. If the timing is wrong, the bot learns reflexes that fall apart the moment it plays a real match. And unlike a reward weight, you cannot change the action space halfway through a run — a checkpoint is welded to the one it trained with.

## The eight control inputs

RocketSim represents a controller state as a `CarControls` struct. RL Studio uses it directly, in this field order:

| Field | Type | Range | What it does |
|---|---|---|---|
| `throttle` | float | -1..1 | Forward/reverse drive on the ground |
| `steer` | float | -1..1 | Turn left/right on the ground |
| `pitch` | float | -1..1 | Nose up/down in the air |
| `yaw` | float | -1..1 | Nose left/right in the air |
| `roll` | float | -1..1 | Barrel-roll left/right in the air |
| `jump` | bool | 0 or 1 | Jump, double jump, or dodge |
| `boost` | bool | 0 or 1 | Burn boost |
| `handbrake` | bool | 0 or 1 | Powerslide |

Five floats, three booleans. That is the whole vocabulary. Every save, every aerial, every ceiling shot a human has ever hit is a sequence of these eight numbers.

The first two act on the ground and the next three act in the air. RocketSim decides which by counting wheels: a car is on the ground when three or more wheels are touching something. On the ground, `steer` sets the wheel angle and `handbrake` cuts lateral tyre friction so the car slides. In the air, `pitch`, `yaw` and `roll` apply torque, and `throttle` still contributes a small forward push — about 67 uu/s². "uu" is *unreal units*, Rocket League's distance unit; for scale, the ball's collision radius is about 91 uu. Boost accelerates an airborne car at roughly 1058 uu/s², about sixteen times harder. So treat air throttle as close to irrelevant rather than exactly zero.

On a real controller the left stick does double duty: pushing it left steers on the ground and yaws in the air, and the same physical button is handbrake on the ground and air-roll in the air. RocketSim gives your bot separate fields, so it never has to think about that binding. It asks for roll directly.

One more physics detail that matters later. When the bot presses `jump` while already airborne, RocketSim computes the dodge direction as `(-pitch, yaw + roll, 0)`. Yaw and roll are **added together** for a dodge. A left dodge from full yaw and a left dodge from full roll are the same dodge.

## Two ways to turn a network into controls

The policy network outputs numbers. Something has to turn those numbers into a `CarControls`. There are two standard designs, and `env.action_set` picks between them.

### Continuous control

Set `env.action_set` to `continuous` and the policy does not pick from a menu. For each of the five analogue axes — throttle, steer, pitch, yaw and roll — it outputs two numbers. The first is a **centre**: where it wants that axis to sit. The second is a **spread**: how far from that centre it is willing to wander on this decision. Those two numbers describe a bell curve. RL Studio draws one random value from that curve and then bends the result into the -1..1 range the controller accepts.

The names for those pieces, now that you have the mechanism: the bell curve is a **Gaussian**, its centre is the **mean**, and its spread is the **standard deviation**. The bend is a `tanh`, an S-shaped function that maps any number onto -1..1 and flattens out smoothly at both ends instead of clipping at a hard corner.

The three booleans work differently. Each gets a single probability — the chance that button is held for this decision.

What this buys you is genuine analogue control. The bot can hold `steer = 0.37` to trace a wide arc into a corner, or feather `pitch = -0.12` to hold an aerial's nose steady. Nothing in the mechanics of the game is out of reach.

What it costs you is a much harder exploration problem. Early in training the distribution is close to random noise, so a random continuous action is a random point in an eight-dimensional box. The car twitches. It does not drive. It can take a very long time to accidentally produce the several seconds of coherent forward driving needed to touch the ball once. Continuous heads carry a second hazard. The network learns the spread as well as the centre, and nothing stops it learning a very small spread. Once it does, every sample is essentially the same control state, and the bot stops trying anything new — it freezes into one behaviour. The discrete version of that disease is entropy collapse, covered in [Exploration and Entropy](exploration.md); read that page for the shape of the failure and the cure, not for the Gaussian arithmetic, because it is written around the 90-entry table.

### Discrete control — RL Studio's default

Set `env.action_set` to `lookup` (the default) and the policy instead outputs one number per entry in a fixed lookup table of complete control states. Those raw scores are called **logits**. A softmax turns them into 90 probabilities that sum to 1, and during training RL Studio draws one entry from that distribution. The entry is copied verbatim into `CarControls`. For evaluation and for the viewer, RL Studio takes the highest-probability entry instead of rolling the dice — see [Exploration and Entropy](exploration.md), which explains why the same checkpoint plays noticeably better that way.

This is the community default for Rocket League for one reason: **the table contains only sensible combinations**. There is no entry for "half throttle, slight left, handbrake on, pitching down". A random sample from the table is full-throttle-and-turn, or boost-straight, or jump-and-dodge-forward — a real driving input. So a freshly initialised bot, picking uniformly at random, produces something that looks like a confused player rather than a seizure. It stumbles into its first ball touch far sooner than a continuous head does, and once the ball is being touched the reward signal starts flowing. (RL Studio has not been benchmarked on this yet, so treat "far sooner" as the claim and do not use a stopwatch figure from anywhere else as a target — the answer depends on your arena count and your hardware.)

A discrete head is also cheaper and better-behaved. The continuous head has to maintain eight separate distributions: one per axis for the five analogue axes, and one per button for the three booleans. Every one of those is another thing that can go wrong. The discrete head has one categorical distribution over 90 entries, and no standard deviation that can collapse.

That does not mean a discrete policy cannot freeze. A categorical distribution can go near-one-hot and stop exploring, exactly as a Gaussian's standard deviation can go to zero. The quantity to watch is entropy either way — see [Exploration and Entropy](exploration.md). What a discrete head avoids is one specific failure: a spread parameter that is learned separately from the action itself and can run away on its own.

## Where the number 90 comes from

The table is built by enumerating combinations and dropping the ones that are redundant or nonsensical. It has two halves.

**Ground actions.** Throttle in {-1, 0, 1}, steer in {-1, 0, 1}, boost in {0, 1}, handbrake in {0, 1}. That is 3 × 3 × 2 × 2 = 36. Then drop every entry where boost is on but throttle is not full forward, because boosting while coasting or reversing is not a thing anyone does on purpose. Boost-on entries number 3 × 3 × 2 = 18; the 6 of those with full throttle survive; 12 are dropped.

```
36 - 12 = 24 ground actions
```

**Aerial actions.** Pitch, yaw and roll each in {-1, 0, 1}, jump in {0, 1}, boost in {0, 1}. That is 3 × 3 × 3 × 2 × 2 = 108. Two rules trim it:

- Drop every entry where `jump` is pressed and `yaw` is non-zero. Remember `dodgeDir = (-pitch, yaw + roll, 0)`: roll already covers sideways dodges, so yaw-while-jumping is a duplicate. That removes 2 (non-zero yaws) × 3 × 3 × 2 = 36 entries.
- Drop every entry where pitch, roll and jump are all zero. Those are the do-nothing and steer-only cases, which the ground half already covers. That removes 3 (yaws) × 2 (boosts) = 6 entries.

The two rules never overlap — one needs `jump = 1`, the other needs `jump = 0`.

```
108 - 36 - 6 = 66 aerial actions
24 + 66 = 90
```

Ground entries also set `yaw = steer`, so the same entry means "turn left" on the ground and "yaw left" in the air. That is what lets one table cover both halves of the game.

Aerial entries set `throttle = boost` for a different reason. Air throttle barely does anything — that 67 uu/s² from earlier — so tying it to the boost button costs nothing while the car is airborne. What it buys is the case where the bot picks an aerial entry while the wheels are still down: instead of coasting, the car at least drives forward. That matters more than it sounds, because the bot picks from one table and is not told which half of it applies right now.

A note on where this lives. At the time of writing, `engine/src/env/` is an empty directory — the action set has not been committed yet. The counts above describe the table RL Studio implements. Once the code lands, that file is the authoritative list, and if it ever disagrees with the arithmetic here, the code wins and this page is the thing to fix.

## What discretisation actually costs

Be honest about this: **the bot cannot steer at 0.37.** It steers full left, straight, or full right. Nothing in between exists in the table.

It gets away with this because it makes 15 decisions per second. To trace a curve that needs about a third of full lock, it steers full-left on one decision and straight on the next two, over and over. The car's own inertia smooths the rest. This is why discrete bots do not look jerky on replay — they are pulse-width-modulating the stick.

But something is genuinely lost. A human's tiny sustained corrections during a long aerial, or a delicate dribble where the ball sits on the roof, need pressure the table cannot express. High-level discrete bots work around it; they do not fully recover it.

Whether that loss is the thing standing between you and a top-level bot is an open question, and the evidence currently points the other way. The strongest published Rocket League RL bots all use the discrete table, and no continuous-head Rocket League policy has been shown to beat them on mechanics. It has not been demonstrated that quantisation, rather than the exploration and stability penalty a continuous head pays, is the binding constraint. So `continuous` is worth trying as an experiment if you want to probe the mechanical ceiling yourself — but treat it as a research direction with a slower and more fragile start, not as the upgrade path.

## Tick skip

RocketSim's physics run at 120 Hz. The bot does not decide 120 times per second. It decides once every `env.tick_skip` ticks — default **8** — and the chosen control state is held for all 8 ticks.

```
120 Hz / 8 ticks = 15 decisions per second
1000 ms / 15 = 66.7 ms per decision
```

Lower `tick_skip` means finer control and more decisions per second of game time. It also costs more, though not in the way people usually assume. `tick_skip = 4` doubles the number of network forward passes for the same amount of simulated Rocket League. The physics cost does not change at all — RocketSim still steps 120 times per game-second either way. So how much throughput you actually lose depends on which side is your bottleneck. If the GPU forward pass dominates, expect game-seconds-per-hour to roughly halve. If you are CPU-bound on physics — check the collect/update phase indicator described in [the training loop](./training-loop.md) — you will lose much less. Run `rl-engine bench` before and after rather than assuming.

Lowering it also silently shortens your planning horizon. `ppo.gamma` discounts per *decision*, not per second — see [Policies, Values and Advantage](policy-and-value.md) for the seconds-per-gamma arithmetic. A gamma that covers roughly 6.7 seconds at 15 Hz covers roughly 3.3 seconds at 30 Hz. Halving `tick_skip` without raising `gamma` makes your bot half as far-sighted.

Higher `tick_skip` gives cheaper training and a longer effective horizon, at the cost of clumsier control. At 16 the bot decides every 133 ms, which is too coarse to correct an aerial mid-flight.

8 is the community default because it is the point where control is still good enough for high-level mechanics and the compute cost is bearable.

## Action delay

`env.action_delay` — default **7 ticks**, which is about 58 ms at 120 Hz — means the controls the policy chooses now do not reach the car immediately.

Nothing is dropped and the car is never uncontrolled during that gap. For those 7 ticks it keeps executing the previous decision's controls. What shifts is the alignment: by the time your chosen input takes effect, the world has already moved on from the observation you chose it from.

The obvious question, given a 7-tick delay inside an 8-tick decision window, is whether your action then only drives the car for the one remaining tick. It does not. The delay staggers the whole schedule rather than eating into a fixed window, so each action still drives the car for a full 8 ticks — it simply starts 7 ticks later than the observation that produced it. Every action gets its complete turn; the pipeline is just one decision deep. (As with the action table, `engine/src/env/` is empty at the time of writing, so this describes the design RL Studio implements rather than committed code. If the code lands and disagrees, the code wins and this page is the thing to fix.)

Deliberately making the problem harder sounds wrong. It is correct here.

A bot trained with zero delay learns reflexes calibrated to a world where its decisions are instant. Real deployment is not that world. There is frame time, input polling, and on a live RLBot connection there is process and network latency. A zero-delay bot moved into a real match is calibrated for inputs that land instantly, so in a real match every input lands about 60 ms late. It arrives at the ball fractionally wrong, and mechanics that depended on exact timing — flip resets, ceiling shots, precise 50/50s — stop landing. The reverse mismatch is just as bad in the opposite direction: a bot trained with delay and deployed without it fires every input about 60 ms early. Training with the delay you will actually deploy under makes the bot learn timings that survive contact with a real match.

Delay is the reason the observation has to include the actions the bot recently chose.

Think about what an in-flight action looks like from the outside. It has not touched the car yet, so it has changed nothing about the ball, the car, or anything else the observation measures. It left no trace in the current frame — and no trace in any earlier frame either, because it did not exist yet when those were taken. No amount of observation history can reveal it. The network has to be told directly, which is why RL Studio appends the last few chosen actions to the observation vector. Formally: with an action delay of `k` ticks, the state that satisfies the Markov property is the world state *plus* the `k` ticks' worth of actions still in flight.

> **Misconception:** "raise `env.obs.frame_stack` to fix action delay." It does not address the cause, and it is expensive — 4 frames means four times the observation memory per arena and therefore fewer parallel arenas. Frame stacking is a separate tool. It supplies derivative information a single frame lacks, such as velocity and spin, and RL Studio's observation already carries velocities outright. See [What the Bot Sees](observations.md), which is also where the observation's field list lives — that list does not yet call out the recent-action block described here, and it should.

## The matching rule

**The action set, the tick skip and the action delay used at deployment must be identical to the ones used in training.** This is not a guideline.

A bot trained at `tick_skip = 8` and run at `tick_skip = 4` sees the world advance only half as far between decisions, and holds each control state for half as long. Everything appears to move at half the speed it learned in, and every dodge, turn and boost burst it commits to is cut in half. Every distance it estimated, every lead it took on a bouncing ball, every dodge timing is now wrong by a factor of two. It will look drunk. The same is true for a mismatched action delay, and a mismatched action table is worse still: entry 47 in a different table is a different control state, so the policy's output is scrambled rather than merely mistimed.

RL Studio stores all three values in the checkpoint. Loading a checkpoint into a configuration that disagrees with it fails with an explicit error naming the mismatched field. It does not quietly adapt, because a quietly-adapted bot looks like a training failure and you would spend a night debugging the wrong thing.

## What to look for in RL Studio

- **The action-distribution bar chart in the viewer** (`lookup` mode). One bar per table entry, height equal to the probability the policy assigned it this frame. Watch it while the bot drives. On the ground you should see mass concentrated on the ground half of the table and shifting as it turns; the instant the car leaves the ground the mass should jump to the aerial entries. If it does not move at all as the situation changes, the policy is ignoring its observation. With `env.action_set = continuous` there are no table entries and this panel does not apply; the equivalent readout is a mean and a spread per analogue axis, and a spread that has shrunk to near zero on every axis is the continuous form of entropy collapse. That panel is not built yet — like `engine/src/env/`, it is described here ahead of the code.
- **The `decisions/sec` figure in the run header.** With `tick_skip = 8` this reads 15 per agent. If you change `tick_skip`, confirm this number changed the way you expected before you spend GPU hours on the run.
- **The `action_set`, `tick_skip` and `action_delay` fields in the checkpoint summary.** Check these before you deploy a bot, and check them again when a checkpoint that trained well plays badly. A mismatch here is the first thing to rule out.
