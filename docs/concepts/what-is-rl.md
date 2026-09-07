# What Reinforcement Learning Is

**Reinforcement learning is a loop: the bot looks at the game, picks a control input, the simulator advances, and the bot gets back a single number saying how that went — repeated millions of times until the numbers add up to a bot that can play.**

## Why this matters

Every other page in these docs is a detail inside that loop. PPO — Proximal Policy Optimization, the specific learning algorithm RL Studio uses — is how the bot changes after the loop produces data; [ppo.md](./ppo.md) is the page about it. Rewards are the number the loop hands back. Observations are what the bot sees at the top of the loop. If the loop itself is clear, the rest of the docs are elaborations. If it is not clear, everything later will feel like arbitrary machinery.

The loop is also the thing you are watching when you press Train. The episode counter ticking up on the dashboard is this loop, running a few hundred times over in parallel.

## The loop, in plain language

Picture one car on the field. Fifteen times a second, roughly, the following happens. That number is not the physics rate, and where it comes from is explained a few paragraphs down under tick skip.

1. The bot reads the current state of the match: where its car is, how fast it is going, where the ball is, where the opponent is, how much boost it has.
2. The bot picks a control input — some combination of throttle, steering, jumping, boosting.
3. The simulator applies that input and advances the physics.
4. The bot receives one number describing how good that step was, and a new reading of the match.

Then it does it again. Nothing else happens. There is no coach, no commentary, no correct answer supplied from outside. There is a number, and there is the next situation.

## The formal names

Now the vocabulary, because the rest of the field uses it and you will hit it in every paper.

- **Timestep** — one pass around the loop. Timesteps are numbered: `t = 0` at kickoff, then 1, 2, 3. A little `t` written under a symbol means "the value of that thing at timestep t". You will meet `o_t`, `a_t` and `r_t` further down this list; the letters stand for observation, action and reward, and each one is defined where it appears.
- **Agent** — the thing making decisions. In RL Studio the agent is a neural network. It takes numbers in and puts numbers out.
- **Policy** — the rule the agent uses to turn an observation into an action. In RL Studio the policy *is* the neural network: feed it the observation vector and it gives you a probability for each control input it could pick. That list is finite. RL Studio's default action space is a fixed table of 90 preset control combinations, so the network's output is 90 numbers that add up to 1. [actions.md](./actions.md) shows where the 90 comes from and what the alternative continuous mode costs you. When people say "the bot improved", they mean the policy changed. Written `π` in papers, and `policy(obs)` in the code below. [policy-and-value.md](./policy-and-value.md) is the page about it.
- **Environment** — everything the agent does not control. Here that is [RocketSim](https://github.com/ZealanL/RocketSim) simulating Soccar: the car physics, the ball, the walls, boost pads, the goal detection, and the opponent car. That last one is worth pausing on: by default the opponent is a frozen copy of the bot itself, so the environment gets harder as the bot gets better. [self-play.md](./self-play.md) is the page about that, and it is why the reward curve alone cannot tell you whether the bot improved.
- **Observation** — the vector of numbers the agent reads at one moment. Written `o_t`.
- **Action** — the control input the agent chooses. Written `a_t`.
- **Reward** — one number the environment returns after the action. Written `r_t`.
- **Done** — a flag saying this step was the last one of the episode. An **episode** is one stretch of play from kickoff to an ending — in RL Studio, either a goal or the clock running out. The "Episodes" section below is about why those two endings are not the same thing.
- **Transition** — the record of one pass: observation, action, reward, next observation, done. Those five parts are the core of the record, and they are what the concept is. RL Studio actually stores three more fields alongside them, because the update step needs them: the probability the policy assigned to the action it picked, the value network's estimate for that state, and a separate `truncated` flag that says whether `done` came from a goal or from the clock. [training-loop.md](./training-loop.md) covers the first two; the "Episodes" section below is about why the third one has to be there.

You will not need the `o_t` / `a_t` / `r_t` subscript notation on this page — the code below uses plain names like `obs` and `reward` — but every RL paper you read uses it, and [policy-and-value.md](./policy-and-value.md) starts using it in earnest.

Note what a timestep is not. It is **not** one physics tick. RocketSim runs Soccar physics at 120 ticks per second. The bot does not make 120 decisions per second — it makes a decision, then that same decision is held for several ticks before the next one. The mechanism is called tick skip, and the setting is `env.tick_skip`. At the default of 8, one decision is held for 8 physics ticks, giving 120 / 8 = 15 decisions per second. Turn it down and the bot decides more often and reacts faster, but every second of game time costs more decisions to simulate and to store. Turn it up and training gets cheaper per second of play, but the bot cannot react to anything faster than its decision interval. [actions.md](./actions.md) has the full trade-off. For this page, the thing to hold onto is: one timestep is one decision, and roughly fifteen decisions happen per second of Rocket League time.

## The loop as code

```python
obs = env.reset()            # kickoff: t = 0
done = False

while not done:
    action = sample(policy(obs))                  # the network gives a distribution
                                                  # over actions; we draw one
    next_obs, reward, done = env.step(action)     # the simulator advances

    buffer.add(obs, action, reward, next_obs, done)   # store the transition

    obs = next_obs
    # t = t + 1
```

Note the sampling. The policy does not output one action — it outputs a probability for every action, and the loop draws from that. Because the draw is random, the bot sometimes picks an action it does not currently rate highest, which is the only way it ever finds out that a different action was better. That is **exploration**, and the section further down is about how to control it.

That is the whole idea. The engine runs this loop for hundreds of arenas at once, and every so often it stops, takes the buffer full of transitions, and updates the network. [training-loop.md](./training-loop.md) walks through what that update pass looks like end to end.

## The reward is a number, not an instruction

Here is the misconception that trips up almost everyone.

**The misconception:** "The reward tells the bot what to do."

**The correction:** the reward only tells the bot how good the last step was, after the fact. It is a single scalar — one floating-point number per timestep. It is not a description. It is not a label saying "you should have flipped left". It is not a score the bot can peek at before choosing. It arrives *after* the action, and it says nothing about which other action would have been better.

So a reward of `-0.3` does not mean "steer harder". It means "that step scored -0.3", and nothing more. On its own that number is not even good or bad — it sits on whatever scale you chose when you wrote the reward function. If the bot usually collects -1.0 per step, then -0.3 was a good step. The absolute size of a reward matters far less than the differences between the rewards of the actions the bot was choosing between.

Whether -0.3 was good or bad depends on what the bot could reasonably have expected from that situation. A -0.3 while sprinting back to cover an open net may be better than average. A -0.3 with the ball sitting on the open goal line is dreadful. Turning a raw reward into "better or worse than expected" is a separate job, done later, and it is what **advantage** does — see the credit assignment section below.

The bot has to work out, from thousands of such numbers across thousands of situations, which of its habits produce good numbers and which produce bad ones. [rewards.md](./rewards.md) covers how to design that number, and what happens when you design it badly.

## Credit assignment: the central difficulty

The bot touches the ball. Three seconds later the ball crosses the line and a goal reward lands.

Three seconds is about 45 decisions. Which of those 45 deserves the credit? The touch itself, obviously — but also the boost pickup eight decisions before it, and the decision to turn toward the ball instead of back-post. Meanwhile some of those 45 decisions were the bot spinning uselessly in the air while the ball flew in on its own momentum. Those deserve nothing.

Nobody hands the algorithm this breakdown. This is the **credit assignment problem**, and it is the central difficulty of reinforcement learning. Two tools address it, and they get their own pages:

- **Discounting** — rewards that arrive sooner after a decision count for more than rewards that arrive later. See [policy-and-value.md](./policy-and-value.md).
- **Advantage estimation** — comparing what actually happened against what the bot expected to happen from that situation, so credit lands on the decisions that beat expectations. See [gae.md](./gae.md).

## Episodes

An **episode** is one stretch of play from kickoff to an end condition. In RL Studio an episode ends in exactly two ways.

- **A goal is scored.** RL Studio treats this as the real end of the episode: it stops the arena here and treats the final state as having no future at all. (A real match would carry on from kickoff with the score changed. Cutting the episode at the goal is a modelling choice, and it is the standard one.)
- **The episode time limit is reached.** Play was cut off by the clock while it was still going. The ball was still rolling, and there was a future — you just stopped looking at it.

These two endings are called **termination** (the goal) and **truncation** (the time limit). They look identical in the loop above — both set `done = True` — but the learning mathematics must treat them differently, because in one case the future really was empty and in the other case it was not. [gae.md](./gae.md) explains exactly what changes.

When an episode ends, the arena resets to a fresh kickoff and `t` goes back to 0.

## The Markov property

Two words that have been used loosely so far now need separating. The **state** is the true, complete situation in the simulator — every position, velocity and timer RocketSim is tracking. The **observation** is the slice of it that gets packed into a vector and handed to the bot. They are not the same, and the gap between them is what this section is about.

An observation is **Markov** if everything you need to predict the future is in it, and knowing the past adds nothing. Plain version: if I show you this observation and nothing else, are you missing anything?

A single still photograph of Rocket League is **not** Markov. You can see where the car is. You cannot see how fast it is going, or which way it is spinning, or whether it is rising or falling. Two identical-looking photographs can lead to completely different next seconds.

RL Studio's observation is not a photograph. It contains linear velocities and angular velocities alongside positions, which is exactly what the photograph was missing. That makes it close to Markov — close enough that the algorithms in these docs work well. It is not a guarantee: whether some particular piece of hidden information is present depends on which fields the observation builder includes. [observations.md](./observations.md) lists them.

This matters practically. If the bot behaves erratically in situations that look identical to you, one honest hypothesis is that they are *not* identical to the bot, and something it needs is missing from the observation.

## Why not supervised learning?

Supervised learning needs labelled examples: input, correct output, repeat. To train a Rocket League bot that way you would need the correct control input for each of a million game states. There is no answer key. Even a Grand Champion cannot write out the correct throttle-steer-jump combination for a million frames, and for most frames there are several good answers anyway.

Reinforcement learning never needs the correct action. It only needs a number saying how the chosen action turned out, and that number is cheap: RocketSim already knows where everything is and how fast it is moving, so a reward function you configure scores every step with no human in the loop. Deciding what that function rewards is real design work — [rewards.md](./rewards.md) is about it — but you write it once and it then scores millions of steps unattended.

**Behaviour cloning is a real alternative, and it is worth being honest about.** Take recorded replays of human matches, treat each frame's observation as the input and the human's actual control input as the label, and train with ordinary supervised learning. This works, and it is often much faster than RL at producing something that looks like Rocket League. What it gives you is an imitation of the demonstrator's *average* behaviour, capped at roughly the demonstrator's skill. What it does not give you is improvement past the data — a cloned bot has no mechanism for discovering a play the demonstrator never made. Serious projects sometimes clone first and then improve with RL. RL Studio is built around the RL half because that is the half this project exists to teach, not because cloning is worse.

## Exploration and exploitation, named

The bot faces a standing tension. It can repeat what has worked so far — **exploitation** — or it can try something different to find out whether something better exists — **exploration**. Too much exploitation and it locks onto the first mediocre habit it stumbles into, forever. Too much exploration and it never commits to anything long enough to get good at it.

Here is what too much exploitation looks like in practice. Early on, the bot discovers that driving straight at the ball produces a small positive reward every time. That is a real improvement over spinning in circles, so it repeats it. It never jumps, so it never touches an airborne ball, so it never finds out that aerials exist — and the reward for aerials never appears in its data, because it never generates a step that would earn one. The bot is not stuck because aerials are hard. It is stuck because it stopped trying things.

This problem is real and it will bite you. [exploration.md](./exploration.md) covers how RL Studio manages it.

## Settings that shape the environment side of the loop

Five settings define what the environment even is. Full details will live in the settings reference (`docs/settings-reference.md`), which is not yet written; until it lands, [training-loop.md](./training-loop.md) covers the arena and batch settings and [actions.md](./actions.md) covers tick skip.

- **`env.game_mode`** — which Rocket League mode the arena simulates. Soccar is standard play and what these docs assume; switching to another mode changes the rules of the game, so a policy trained in one will not transfer to another.
- **`env.team_size`** — cars per team, 1 for 1v1 up to 3 for 3v3. Turn it up and the bot must learn positioning and rotation, but credit assignment gets much harder because teammates affect the reward too. Turn it down for the cleanest possible learning signal.
- **`env.num_arenas`** — how many independent matches run in parallel. Turn it up and each iteration's fixed budget of `ppo.steps_per_iteration` transitions — the number of transitions collected before the network is updated once — is collected in less wall-clock time, gathered from more, shorter slices of play; turn it too far up and you run out of CPU threads or RAM. This is the main throughput knob — it changes how fast a batch is filled, not how big the batch is.
- **`env.max_episode_seconds`** — how much simulated game time an episode may run before it is truncated. Turn it up and the bot sees longer, more realistic passages of play; turn it down and you get more kickoffs per hour, which speeds up early learning but stops the bot from ever experiencing a long possession.
- **`env.tick_skip`** — how many 120 Hz physics ticks each decision is held for, covered above. Default 8, giving 15 decisions per second. See [actions.md](./actions.md).

## What to look for in RL Studio

Open the dashboard while a run is going.

- The **episode counter** in the run header increments every time any arena hits a goal or a time limit. With a few hundred arenas it climbs fast. That counter is the loop on this page, completing.
- The **Episode Length** panel plots the mean number of agent steps per episode. Early in training it sits pinned at the ceiling set by `env.max_episode_seconds`, because the bot almost never scores on purpose and nearly every episode is cut off by the clock. Note the units: the panel counts decisions, not seconds. At the default `env.tick_skip` of 8 the bot makes 15 decisions per second, so a 30-second limit shows up as a flat line near 450. Expect a little noise below the limit even on the first iteration — a random bot does bump the ball in by accident, sometimes into its own net. The signal to watch for is the *mean* line trending down and staying down. That means goals are being scored. Read it as "the ball is now reaching a net", not as "the bot is getting better": in self-play the other car is the bot itself or a frozen copy of it, so an own goal shortens an episode exactly as much as a scored goal does. Pair it with **Goals per Episode** and **Ball Touch Rate**, and with the **ELO** panel, which plays fixed benchmark opponents that never train. [reading-the-graphs.md](./reading-the-graphs.md) and [self-play.md](./self-play.md) cover why the reward curve alone cannot settle this.
- The **3D viewer** shows one arena playing live. What you are watching is `obs -> policy -> action -> step` at roughly fifteen decisions per second. RocketSim is stepping 120 physics ticks per second underneath that view, and at the default `env.tick_skip` of 8 only every eighth tick is a decision point. Each of those becomes one transition in the buffer. The seven ticks in between run under a control input the bot already chose, which is why the car looks like it commits to a line rather than twitching. The viewer draws at your monitor's refresh rate rather than at the 120 Hz physics rate, so do not try to count decisions by counting frames on screen — read `decisions/sec` in the run header instead.

Next: [policy-and-value.md](./policy-and-value.md), which covers what the network actually outputs and how future rewards get counted.
