# What the Bot Sees

**An observation is a flat list of numbers describing the current game state, built by hand from the simulator's variables, scaled so every number sits roughly between -1 and 1.**

## Why this matters

In [what-is-rl.md](./what-is-rl.md) the agent-environment loop starts with an observation. In [policy-and-value.md](./policy-and-value.md) the policy network turns that observation into an action and the value network turns it into a number. Both of those are functions of the observation and nothing else. If a fact is not in the vector, the bot cannot use it. If a fact is in the vector but expressed badly, the bot will spend thousands of games learning to undo the bad expression.

Observation design is the part of this project where a few careful decisions buy you more than any hyperparameter will. It is also where a silent bug does the most damage, because a wrong observation does not crash. It just produces a bot that never gets good, and you will blame PPO.

## The bot does not see pixels

There is no camera. RL Studio never renders a frame for the network. The 3D view you watch is for you.

Instead, on every decision step, the observation builder reads the simulator's state directly and writes numbers into an array. These are the groups it writes:

- **The ball.** Position, linear velocity, angular velocity.
- **The acting car** — the car this observation is for. Position, orientation, linear velocity, angular velocity, boost amount, and a set of boolean flags: on the ground, has a jump available, has a flip available, is currently flipping, is demolished.
- **Every teammate**, and then **every opponent**, given twice: their own state, and their state expressed relative to the acting car.
- **Boost pads.** Soccar has 34 of them, 6 big and 28 small, at fixed locations. For each pad, whether it is currently available, and if not, how long until it respawns. (Big pads take 10 seconds, small pads 4.)

That is one observation. One car, one moment. In a 2v2, four observations are built per decision step, one from each car's point of view, and all four go into the training data.

> **Misconception:** "the bot sees the game." It does not. It sees the fields somebody chose to put in the vector. If you never include boost pad timers, your bot cannot learn to arrive at a corner boost the instant it comes back, no matter how long you train it. That behavior is not hard for it — it is invisible to it.

## Relative features beat absolute ones

Suppose the observation only tells the bot where things are in world coordinates: "ball at x=1200, y=-3400" and "I am at x=900, y=-3900".

The bot has to learn that this specific pair of positions means "the ball is up and to my right, go get it". Then it has to learn the same lesson again for the mirrored corner. And again mid-field. And again for every other combination of the two positions. The world is big, and world coordinates give the network no reason to believe that two situations which *feel* identical to a human are related at all.

Now express the ball in the acting car's own frame: "the ball is 900 units ahead of me and 300 to my right, closing at 400 units per second." That single description covers every corner of the field at once. One lesson, learned once, applies everywhere.

So RL Studio computes, for the ball and for every other car, the offset and relative velocity rotated into the acting car's frame — using the car's forward, right and up vectors as the basis. It also keeps the absolute positions. That redundancy costs a few dozen extra inputs and roughly nothing in compute, and absolute position genuinely matters sometimes: which half of the field you are in decides whether a clear is a clear or an own goal. Give the network both and let it decide which to use.

## Orientation is three vectors, not three angles

A car's orientation could be described with pitch, yaw and roll — three numbers. RL Studio does not do that. It feeds the rotation matrix instead: RocketSim's `RotMat` exposes `.forward`, `.right` and `.up`, three unit vectors, nine numbers total.

The reason is wraparound. Yaw is an angle, and an angle wraps: 179 degrees and -179 degrees are two degrees apart in reality but 358 apart as numbers. A neural network is a smooth function of its inputs. Asking it to treat two far-apart inputs as nearly identical, and to do that only at one particular seam, is asking it to learn a discontinuity — which is exactly what smooth functions are bad at. Euler angles also suffer gimbal lock, where two of the three axes collapse onto each other and the representation stops being unique. In a game where cars spend a third of their time upside down in the air, that matters.

The three basis vectors have no seam. Rotate the car a little and every one of the nine numbers moves a little. That is what you want.

## Normalization, and the constants that do it

Raw game units are on wildly different scales. A y-coordinate runs to about 5120. Boost runs 0 to 100. A boolean is 0 or 1.

Feed that straight into a network and the first layer's weights face a problem. The weight on the y-position input and the weight on the on-ground flag both start near zero and both get updated by gradient descent, but the gradient with respect to a weight is proportional to the input it multiplies. So the y-position weight sees gradients thousands of times larger. You end up needing a learning rate small enough not to blow up the large inputs, which is far too small to make progress on the small ones. Training is slow, unstable, or both.

The fix is to divide each field by its largest plausible magnitude, so everything lands in roughly -1 to 1. RL Studio uses RocketSim's own constants from `RocketSim::RLConst`, never magic numbers:

| Field | Divide by | Constant |
|---|---|---|
| Position, x | 4096 | `ARENA_EXTENT_X` |
| Position, y | 5120 | `ARENA_EXTENT_Y` |
| Car velocity | 2300 | `CAR_MAX_SPEED` |
| Ball velocity | 6000 | `BALL_MAX_SPEED` |
| Car angular velocity | 5.5 rad/s | `CAR_MAX_ANG_SPEED` |
| Ball angular velocity | 6.0 rad/s | `BALL_MAX_ANG_SPEED` |
| Boost | 100 | `BOOST_MAX` |

"Roughly" is doing real work in that sentence. `ARENA_EXTENT_Y` excludes the inner goal, so a ball sitting in the back of the net normalizes to slightly past 1.0. That is fine. The goal is to get every input onto a comparable scale, not to enforce a hard bound.

The setting is **`env.obs.normalize`**. Leave it on. Turning it off is worth doing exactly once, as an experiment: run the same config with it on and off, and watch the off run take far longer to move at all. That is the lesson. It is not a tuning knob.

## One policy, two teams: the mirror

Blue attacks the orange net. Orange attacks the blue net. Naively that is two different jobs, and you would train two networks — halving your data and doubling your problems.

Rocket League's field is symmetric, so there is a better trick. RocketSim's `PhysState::GetInvertedY()` returns a copy of a physics state rotated 180 degrees about the vertical axis: position, linear velocity, angular velocity, and each of the rotation matrix's three basis vectors are all multiplied by (-1, -1, 1).

Apply that to every physics state before building an orange car's observation, and orange's observation becomes indistinguishable from a blue observation of the mirrored situation. Orange now "believes" it is attacking the same net blue attacks. One set of weights plays both teams, and every episode produces training data from both sides at once.

> **The trap, stated plainly.** The mirror must be applied consistently to the observation *and* to the interpretation of the action that comes back. The policy outputs an action in mirrored coordinates, so "steer right" means right in the mirrored frame. If you mirror the observation but hand the action to the car unmirrored — or mirror only some of the fields — you get a bot that plays well as blue and drives calmly into its own net as orange. It will not look like a crash. It will look like the bot is bad. Test it: run one trained checkpoint against itself and check that blue and orange play at the same standard. A large, stable skill gap between the two sides is a mirror bug, not a training problem.

Because the mirror is a rotation about the vertical axis and not a reflection, the coordinate frame stays right-handed and the physics stays legal. Nothing about the car's handling changes.

## Padding: how one network handles 1v1 through 3v3

A network has a fixed number of inputs. A match does not have a fixed number of cars.

RL Studio resolves this by reserving a fixed number of teammate slots and opponent slots — enough for the largest supported mode — and zero-filling the ones a given match does not use. In a 1v1, the two spare teammate slots and two spare opponent slots are all zeros.

Zeros are ambiguous. A zero-filled slot looks exactly like a real car parked at the field origin with no velocity. So each slot carries an extra number, a **presence flag**: 1 means this slot holds a real car, 0 means it is padding. With that flag the network can learn to ignore padded slots, which is a trivial thing to learn and an impossible thing to guess.

Slot ordering matters just as much. The rule has to be deterministic and stable — for example, sort teammates and opponents by distance to the acting car, or by car id, and use the same rule everywhere. If ordering is arbitrary, then "the nearest opponent is closing on the ball" arrives in slot 1 sometimes and slot 3 other times, and the network has to learn the same lesson separately for every permutation of slots. That is a large amount of wasted capacity spent on nothing.

Distance-sorted ordering has one wrinkle worth knowing: when two cars are nearly equidistant, their slots can swap between consecutive steps, and the observation jumps even though the world barely moved. Sorting by car id avoids the jitter but throws away the useful hint that slot 1 is the car that matters most. Either is defensible. Pick one and never change it mid-run.

## Frame stacking

Frame stacking means concatenating the last N observations into one longer vector, so the network sees a short history instead of a single instant.

There is a reason it might help here. RL Studio applies an action delay — the press the bot chooses now takes effect a few ticks later (see [actions.md](./actions.md)). At the moment of choosing, the consequences of the *previous* press are not fully visible yet. A stack of recent frames lets the network infer what it recently pressed by looking at how the car is already moving.

There is a stronger reason RL Studio usually does not need it. The observation already includes linear and angular velocity for everything, plus jump and flip flags. Velocity is the derivative that a history would otherwise have to supply. That gets the state close to satisfying the Markov property described in [what-is-rl.md](./what-is-rl.md): the current observation is close to sufficient, and the extra frames add little.

The setting is **`env.obs.frame_stack`**. A value of 1 means a single frame, which is the default. Turn it up and the input vector is multiplied by that factor — 4 frames means four times the inputs, four times the first layer's weights, and four times the observation memory held per arena. With hundreds of arenas running in parallel, that memory is not free, and it directly reduces how many arenas fit on your machine. Turn it up only if you have a specific reason and a baseline run to compare against.

## The deployment rule

**The observation builder used during training and the one used at play time must produce the same numbers in the same order.**

This is not a stylistic preference. The network is a fixed function of a fixed input layout. Swap two fields and input number 47 now means "opponent boost" to a network that learned it meant "my z velocity". Change a normalization constant and every downstream activation shifts. There is no graceful degradation here. A reordered or rescaled observation turns a trained bot into a random one, and it will look like the checkpoint is corrupt rather than like a config mismatch.

This bites hardest at the boundary between the trainer and everything else: the viewer, an evaluation run, an RLBot deployment. Those are different code paths, and code paths drift.

RL Studio's defence is that the **observation spec is recorded inside the checkpoint** — the field order, the slot counts, the normalization constants, the frame stack depth, whether the mirror is applied. Anything that loads a checkpoint rebuilds its observation builder from that recorded spec rather than from the current config file, and refuses to run if the spec and the builder disagree. If you add a field to the observation, old checkpoints keep working with the old spec; they do not silently get reinterpreted.

## A note on the exact layout

At the time of writing, `engine/src/env/` is empty — the observation builder is not implemented yet. This page describes the design, not a shipped index table. The authoritative vector length and field order are whatever the engine's observation builder produces, and they are recorded in each checkpoint's observation spec. Read that, or read the builder, before writing code that indexes into an observation by number.

## What to look for in RL Studio

- **The observation inspector panel.** Select a car in the viewer and it shows that car's live observation vector, grouped and labelled, updating every decision step. Watch the ball's relative-position fields as the bot drives past the ball: they should cross zero as it passes. Watch the presence flags in a 1v1: exactly one opponent slot should read 1, the rest 0.
- **The reported obs size in the run header.** One number, printed when a run starts. If it changes between two runs you thought were identical, something in the observation config changed, and their checkpoints are not interchangeable.
- **The checkpoint's recorded observation spec.** Visible on the checkpoint's detail panel. When you load a checkpoint into the viewer, this is the spec the app rebuilds the builder from. If it does not match the current config, RL Studio says so explicitly instead of running a bot with scrambled inputs.
- **Blue versus orange win rate in a self-play evaluation.** It should hover near 50%. A persistent gap is the clearest symptom of a half-applied mirror.
