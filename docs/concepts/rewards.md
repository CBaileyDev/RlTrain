# Designing the Reward

**The reward function is the only place you tell the bot what "good" means, and it will optimise exactly what you wrote there rather than what you meant.**

## Why this matters

Everything else in RL Studio is machinery. The [observation builder](./observations.md) decides what the bot can see, the [action table](./actions.md) decides what it can do, [PPO](./ppo.md) — the learning algorithm — decides how the weights move. None of them decide what the bot *tries to do*. The reward does, alone. Get the architecture slightly wrong and training is slower; get the reward slightly wrong and you train a bot that is excellent at something you never wanted.

This page assumes the loop from [What Reinforcement Learning Is](./what-is-rl.md) — observation, action, reward, episode.

## Start from the honest reward

The reward you actually want is short: `+1` when your team scores, `-1` when the other team scores, `0` on every other step.

This is a **sparse** reward — zero almost everywhere, non-zero at rare moments. It is also the *correct* reward. A bot that maximises it wins games, and there is no way to exploit it.

It also almost never works from a fresh start. A randomly initialised policy samples roughly uniformly from the 90 discrete actions ([What the Bot Can Do](./actions.md)) fifteen times a second — the bot decides once every 8 physics ticks, and the simulator runs at 120 Hz. It drives in circles, jumps at nothing, boosts into the corner. It does not score, and it can run for hours of simulated time without scoring.

Now look at what PPO receives. Every reward is zero, so the value network quickly learns to predict zero everywhere, so the advantages collapse to noise centred on zero. Averaged over a batch, the policy-gradient term comes out to nothing.

The weights do still move, and that makes it worse rather than neutral. The entropy bonus contributes a gradient on every single step regardless of reward, and it pushes the policy toward *more* randomness ([Exploration and Entropy](./exploration.md)). So the one force still acting makes the bot less decisive, and nothing in the update carries any information about scoring. The loop is closed.

Even a lucky goal barely helps. An episode capped at 30 seconds by `env.max_episode_seconds` is about 450 decisions at 15 a second, and one scalar arrived at the end of all of them. Discounting gives a partial answer about which decisions mattered ([Policies, Values and Advantage](./policy-and-value.md)), but one `+1` against thousands of empty episodes is a very quiet signal.

## Shaped rewards: a ladder to climb

A **shaped** (or **dense**) reward pays small amounts frequently for behaviour that points roughly toward the sparse goal. It is a ladder, and each rung has to be reachable by accident from the rung below.

RL Studio's default ladder, in the order a bot climbs it:

1. **Move toward the ball.** A random bot bumps the ball by chance within seconds. Paying for closing velocity turns that accident into a habit within minutes.
2. **Hit it hard.** Once the bot reliably arrives, paying for a strong touch teaches it to arrive at speed rather than nudge.
3. **Hit it toward the opponent's net.** Paying for ball velocity toward goal turns random contact into directed contact.
4. **Score.** Goals now happen on their own, and the sparse reward finally has something to reinforce.

A reward term earns its place if it makes the *next* behaviour discoverable by accident.

## The known-good weights

These are the weights from a working scoring bot, recorded in `docs/internal/GROUNDING.md`. Each term's exact definition lives in [the rewards reference](../rewards-reference.md); here they are an example of *shape*.

| Term | Weight | Paid | Zero-sum |
|---|---|---|---|
| SaveBoost | 0.2 | every step | no |
| AirTime | 0.25 | every step | no |
| FaceBall | 0.25 | every step | no |
| VelocityBallToGoal | 2.0 | every step | **yes** |
| VelocityPlayerToBall | 4.0 | every step | no |
| BoostPickup | 10 | on pad pickup | no |
| Bump | 20 | on bump | **yes** |
| StrongTouch | 60 | on a hard touch | no |
| Demo | 80 | on demolition | **yes** |
| Goal | 150 | on goal | no |

Notice the shape before the numbers. **Continuous per-step terms carry tiny weights; rare event terms carry large ones.** That is arithmetic, not taste. The bot decides every 8 physics ticks at 120 Hz, so 15 times a second, and a per-step term pays its weight on every one of those:

```
VelocityPlayerToBall, driving hard at the ball for 10 seconds
  raw value  ~0.3 to 0.8      (closing speed / CAR_MAX_SPEED)
  steps      10 s x 15 Hz = 150
  total      150 x 4.0 x 0.5  ~=  300

Goal
  total      150, once
```

A weight of `4.0` out-earns a weight of `150` inside ten seconds. The rare event needs a big number just to stay in the conversation.

### Misconception: "a bigger weight makes the bot want it more"

It does not. A bigger weight makes the bot want it more **relative to everything else, per unit of time**. Total earned reward is weight multiplied by how often the term pays, so a large weight on a rare event is routinely dominated by a small weight on a per-step event. Compare `weight x expected rate`, never weights alone.

## Reward hacking

Reward hacking is what happens when your written reward has a cheaper maximum than the behaviour you had in mind. Three you will actually hit:

**The vibrator.** `StrongTouch` at 60 is generous, and the bot discovers it can park against the ball and oscillate, triggering a touch every few steps. On the dashboard: Episode Reward Mean climbs steadily, the `StrongTouch` band swells to dominate the per-term stack, Goals per Episode stays at roughly zero. In the viewer, a car buzzing against a stationary ball at midfield.

**The boost farmer.** `BoostPickup` at 10 a pad, with 34 pads and short respawn cooldowns, can out-earn attacking. The bot learns a loop through the pad grid and stops going near the ball. The `BoostPickup` band grows, `VelocityPlayerToBall` shrinks, goals stay at zero.

**The admirer.** `FaceBall` pays for pointing at the ball. It does not require travelling toward it. The bot sits in its own corner, aims its nose, and collects 0.25 fifteen times a second forever. The give-away is a small but perfectly steady `FaceBall` band with `VelocityPlayerToBall` pinned near zero.

The general rule: **if there is a cheaper way to earn your reward than playing well, the bot will find it.** It has millions of steps and no shame.

## Zero-sum rewards

A **zero-sum** term takes from the other team whatever it gives to yours, so the totals cancel.

```math
z_i \;=\; r_i \;-\; \frac{1}{m}\sum_{k \in \text{opposing team}} r_k
```

- `z_i` — the zero-sum reward finally given to player `i`.
- `r_i` — the raw value of the term for player `i` this step, before adjustment.
- `m` — the number of players on the opposing team.
- The sum runs over every player `i` is playing against.

When both teams are the same size the totals cancel exactly. Worked example, 2v2, `Demo` weight 80. Blue player A demos someone: A's raw value is 80, B's is 0. A receives `80 - 0 = 80` and B receives `0`. Each orange player receives `0 - (80 + 0)/2 = -40`. Sum: `80 + 0 - 40 - 40 = 0`.

**What this prevents:** without it, both teams can inflate their reward together. Two bots trading gentle touches at midfield both collect `StrongTouch` forever and neither wins anything. Cooperative reward farming is a stable, attractive equilibrium in self-play; a term I can only increase by decreasing yours cannot be farmed jointly.

In RL Studio this is the `rewards.zero_sum` list. Above, `VelocityBallToGoal`, `Bump` and `Demo` are zero-sum. `BoostPickup` deliberately is not — taking a pad already denies it to the opponent, and subtracting again would double-count. Zero-sum is what keeps self-play competitive rather than collusive; opponent selection is covered in [Self-Play and Skill Rating](./self-play.md).

## Team spirit

In 2v2 and 3v3 you must decide whether a player is paid for its own contribution or the team's. **Team spirit** blends the two.

```math
b_i \;=\; (1 - \tau)\, r_i \;+\; \tau \cdot \frac{1}{n}\sum_{j \in \text{my team}} r_j
```

- `b_i` — the blended reward used in place of the raw value.
- `r_i` — player `i`'s own raw value for this term.
- `τ` (tau) — the team spirit setting, 0 to 1. This is `rewards.team_spirit`.
- `n` — the number of players on `i`'s own team.
- The sum covers `i`'s whole team including `i`, so the right-hand part is the team mean.

Blending happens first and the zero-sum subtraction is applied to `b_i` afterward. Team spirit only moves reward around inside a team and never changes the team total, so this ordering does not break the zero-sum property.

**τ = 0** pays every player only for what it personally did. In 3v3 that gives three selfish ball-chasers who all challenge the same ball at once with an open net behind them.

**τ = 1** gives every player the team mean, so a teammate's goal pays you exactly as much as your own. That is what makes passing and rotation learnable: dropping back costs you nothing when your teammate finishes. The failure mode is the passenger — a bot that does nothing still collects the team average, so nothing pushes it to contribute.

**τ ≈ 0.3** is the usual starting point. Most reward still tracks your own actions, so laziness is punished, but enough is shared that setting up a teammate is not strictly worse than shooting yourself. Raising τ over training is common: near 0 while individual mechanics form, near 0.5 once the bot can play.

## Weight schedules

A ladder is for climbing off. `VelocityPlayerToBall` is what teaches a random bot to find the ball. Later it is what teaches a competent bot to chase across the field when it should rotate back. The term that got you off the ground becomes your ceiling.

The fix is to decay the shaping and leave the honest reward alone. `rewards.schedule` interpolates weights over training steps:

```jsonc
"rewards": {
  "schedule": [
    { "at_steps": 0,         "VelocityPlayerToBall": 4.0, "FaceBall": 0.25, "Goal": 150 },
    { "at_steps": 100000000, "VelocityPlayerToBall": 2.0, "FaceBall": 0.10, "Goal": 150 },
    { "at_steps": 400000000, "VelocityPlayerToBall": 0.5, "FaceBall": 0.0,  "Goal": 150 },
    { "at_steps": 800000000, "VelocityPlayerToBall": 0.0, "FaceBall": 0.0,  "Goal": 150 }
  ]
}
```

`Goal` never moves. It is the thing you actually want and it is not exploitable, so there is no reason to shrink it.

**Decay too fast** and the reward the bot was optimising vanishes before goal-seeking is self-sustaining. Episode Reward Mean drops off a cliff and behaviour regresses. Step the schedule back and stretch it. **Decay too slow** and the bot plateaus on the shaped behaviour: it ball-chases beautifully, forever, because that is still where the money is. Reward Mean sits high and flat while Goals per Episode stops improving. Schedules stretched over hundreds of millions of steps are normal.

## Scale matters, not only ratios

It is tempting to think multiplying every weight by 100 changes nothing because the ratios are preserved. It is not a no-op.

The value network is trained by minimising squared error against returns. Multiply every reward by 100 and the targets are 100 times larger, the errors are 100 times larger, and the gradients into the value head are roughly 100 times larger. You have quietly multiplied the critic's learning rate by 100, and the value loss curve will show it.

Advantage normalisation absorbs part of this on the policy side by rescaling advantages before the update — see [GAE: Estimating the Advantage](./gae.md). It does not touch the value loss, and it does not rescue the entropy coefficient, which is expressed in absolute units against a loss that just changed size ([Exploration and Entropy](./exploration.md)). Keep total episode reward in the range of tens to low hundreds, and if you do rescale, retune the value learning rate.

## What to look for in RL Studio

- **The stacked per-term reward breakdown.** Each term is a coloured band, height is mean contribution per episode. Read it before anything else. If one band dominates and it is not the band you intended, you have found the bot's real objective.
- **Episode Reward Mean rising while Goals per Episode stays flat.** The signature of reward hacking. Reward going up is not evidence of progress; reward going up *together with the outcome you care about* is.
- **The live reward editor.** Change weights mid-run without restarting. Suspect `StrongTouch` farming? Drop that weight to zero and watch. If Episode Reward Mean collapses, that was the whole strategy.
- **The `rewards.zero_sum` and `rewards.team_spirit` fields** in the config panel, plus the schedule preview graph, which plots each weight against training step so you can see when a term is due to fade.

Reading several curves together when more than one looks wrong is covered in [Reading the Training Graphs](./reading-the-graphs.md).
