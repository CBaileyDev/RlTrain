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

The first two act on the ground and the next three act in the air. RocketSim decides which by counting wheels: a car is on the ground when three or more wheels are touching something. On the ground, `steer` sets the wheel angle and `handbrake` cuts lateral tyre friction so the car slides. In the air, `pitch`, `yaw` and `roll` apply torque, and `throttle` still contributes a small forward push — about 67 uu/s², which is tiny next to boost, so treat air throttle as close to irrelevant rather than exactly zero.

On a real controller the left stick does double duty: pushing it left steers on the ground and yaws in the air, and the same physical button is handbrake on the ground and air-roll in the air. RocketSim gives your bot separate fields, so it never has to think about that binding. It asks for roll directly.

One more physics detail that matters later. When the bot presses `jump` while already airborne, RocketSim computes the dodge direction as `(-pitch, yaw + roll, 0)`. Yaw and roll are **added together** for a dodge. A left dodge from full yaw and a left dodge from full roll are the same dodge.

## Two ways to turn a network into controls

The policy network outputs numbers. Something has to turn those numbers into a `CarControls`. There are two standard designs, and `env.action_set` picks between them.

### Continuous control

Set `env.action_set` to `continuous` and the policy outputs the parameters of a probability distribution over the real-valued sticks — in the usual construction, a mean and a standard deviation per axis, sampled from a Gaussian and squashed into -1..1 with a `tanh`. The booleans get their own per-input probabilities.

What this buys you is genuine analogue control. The bot can hold `steer = 0.37` to trace a wide arc into a corner, or feather `pitch = -0.12` to hold an aerial's nose steady. Nothing in the mechanics of the game is out of reach.

What it costs you is a much harder exploration problem. Early in training the distribution is close to random noise, so a random continuous action is a random point in an eight-dimensional box. The car twitches. It does not drive. It can take a very long time to accidentally produce the several seconds of coherent forward driving needed to touch the ball once. Continuous heads are also more sensitive to the standard deviation collapsing to near zero, which freezes the bot into one behaviour. (That failure mode belongs to [Exploration and Entropy](exploration.md).)

### Discrete control — RL Studio's default

Set `env.action_set` to `lookup` (the default) and the policy instead outputs one number per entry in a fixed lookup table of complete control states. Those numbers become probabilities, and during training one entry is sampled from them. That entry is copied verbatim into `CarControls`.

This is the community default for Rocket League for one reason: **the table contains only sensible combinations**. There is no entry for "half throttle, slight left, handbrake on, pitching down". A random sample from the table is full-throttle-and-turn, or boost-straight, or jump-and-dodge-forward — a real driving input. So a freshly initialised bot, picking uniformly at random, produces something that looks like a confused player rather than a seizure. It reaches the ball by accident within minutes instead of hours, and the reward signal starts flowing.

A discrete head is also cheaper and better-behaved: one categorical distribution instead of eight, and no standard deviation to collapse.

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

Ground entries also set `yaw = steer`, and aerial entries also set `throttle = boost`. That is why a single table works whether the wheels are down or not: the same entry means "turn left" on the ground and "yaw left" in the air.

A note on where this lives. At the time of writing, `engine/src/env/` is an empty directory — the action set has not been committed yet. The counts above describe the table RL Studio implements. Once the code lands, that file is the authoritative list, and if it ever disagrees with the arithmetic here, the code wins and this page is the thing to fix.

## What discretisation actually costs

Be honest about this: **the bot cannot steer at 0.37.** It steers full left, straight, or full right. Nothing in between exists in the table.

It gets away with this because it makes 15 decisions per second. To trace a curve that needs about a third of full lock, it steers full-left on one decision and straight on the next two, over and over. The car's own inertia smooths the rest. This is why discrete bots do not look jerky on replay — they are pulse-width-modulating the stick.

But something is genuinely lost. A human's tiny sustained corrections during a long aerial, or a delicate dribble where the ball sits on the roof, need pressure the table cannot express. High-level discrete bots work around it; they do not fully recover it. If your goal is the last few percent of mechanical precision, `continuous` is the honest answer, and you should expect a slower and more fragile start.

## Tick skip

RocketSim's physics run at 120 Hz. The bot does not decide 120 times per second. It decides once every `env.tick_skip` ticks — default **8** — and the chosen control state is held for all 8 ticks.

```
120 Hz / 8 ticks = 15 decisions per second
1000 ms / 15 = 66.7 ms per decision
```

Lower `tick_skip` means finer control and more decisions per second of game time. It also costs proportionally more: `tick_skip = 4` doubles the number of network forward passes for the same amount of simulated Rocket League, so your throughput in game-seconds-per-hour roughly halves. And it silently shortens your planning horizon. `ppo.gamma` discounts per *decision*, not per second — see [Policies, Values and Advantage](policy-and-value.md) for the seconds-per-gamma arithmetic. A gamma that covers roughly 6.7 seconds at 15 Hz covers roughly 3.3 seconds at 30 Hz. Halving `tick_skip` without raising `gamma` makes your bot half as far-sighted.

Higher `tick_skip` gives cheaper training and a longer effective horizon, at the cost of clumsier control. At 16 the bot decides every 133 ms, which is too coarse to correct an aerial mid-flight.

8 is the community default because it is the point where control is still good enough for high-level mechanics and the compute cost is bearable.

## Action delay

`env.action_delay` — default about **7 ticks, roughly 58 ms** — inserts a lag between the state the policy looked at and the moment its chosen controls start driving the car.

Deliberately making the problem harder sounds wrong. It is correct here.

A bot trained with zero delay learns reflexes calibrated to a world where its decisions are instant. Real deployment is not that world. There is frame time, input polling, and on a live RLBot connection there is process and network latency. A zero-delay bot moved into a real match is a player whose muscle memory is 60 ms early on every single input. It arrives at the ball fractionally wrong, and mechanics that depended on exact timing — flip resets, ceiling shots, precise 50/50s — stop landing. Training with delay makes the bot learn timings that survive contact with a real match.

Delay is also the main reason frame stacking helps. When your action has not taken effect yet, the current observation does not fully describe your situation — you need to remember what you asked for. Stacking the last few observations gives the network the recent history it needs to infer that. See [What the Bot Sees](observations.md).

## The matching rule

**The action set, the tick skip and the action delay used at deployment must be identical to the ones used in training.** This is not a guideline.

A bot trained at `tick_skip = 8` and run at `tick_skip = 4` sees the world arriving at twice the speed it learned in. Every distance it estimated, every lead it took on a bouncing ball, every dodge timing is now wrong by a factor of two. It will look drunk. The same is true for a mismatched action delay, and a mismatched action table is worse still: entry 47 in a different table is a different control state, so the policy's output is scrambled rather than merely mistimed.

RL Studio stores all three values in the checkpoint. Loading a checkpoint into a configuration that disagrees with it fails with an explicit error naming the mismatched field. It does not quietly adapt, because a quietly-adapted bot looks like a training failure and you would spend a night debugging the wrong thing.

## What to look for in RL Studio

- **The action-distribution bar chart in the viewer.** One bar per table entry, height equal to the probability the policy assigned it this frame. Watch it while the bot drives. On the ground you should see mass concentrated on the ground half of the table and shifting as it turns; the instant the car leaves the ground the mass should jump to the aerial entries. If it does not move at all as the situation changes, the policy is ignoring its observation.
- **The `decisions/sec` figure in the run header.** With `tick_skip = 8` this reads 15 per agent. If you change `tick_skip`, confirm this number changed the way you expected before you spend GPU hours on the run.
- **The `action_set`, `tick_skip` and `action_delay` fields in the checkpoint summary.** Check these before you deploy a bot, and check them again when a checkpoint that trained well plays badly. A mismatch here is the first thing to rule out.
