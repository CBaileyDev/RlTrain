# Exploration and Entropy

**A bot that always repeats yesterday's best move never finds a better one, so RL Studio deliberately keeps the policy a little undecided and uses entropy to measure how undecided it still is.**

## Why this matters

Everything your bot ever learns, it first did by accident. The policy cannot be rewarded for an aerial it has never attempted. It cannot learn that a fifty-fifty challenge beats a retreat unless it tries the challenge. Learning in reinforcement learning is a search, and a search needs new places to look.

The problem is that the [policy](./policy-and-value.md) — the network that decides what to press — is also being trained, every iteration, to do more of what already worked. (An *iteration* is one collect-experience-then-update cycle; there are thousands in an overnight run.) That pressure is relentless. Left alone, it will lock onto the first mediocre habit that earns reward and stay there forever. Exploration is the counter-pressure. Entropy is the number that tells you whether the counter-pressure is winning, losing, or already lost — and it is the single fastest way to tell, at a glance, whether a run is worth leaving on overnight.

## The dilemma, in Rocket League terms

Suppose your reward config has `VelocityPlayerToBall` at weight 4.0. Early in training the bot discovers something that works: point the nose at the ball and hold boost. Reward goes up immediately and reliably. That is **exploitation** — doing the best thing you currently know.

Now consider the aerial. A well-timed aerial touch scores far more than ball-chasing ever will: it earns the touch reward, it earns `VelocityBallToGoal`, and eventually it earns goals. But the first hundred aerial attempts are terrible. The bot jumps, pitches back, drifts, misses the ball entirely, lands upside down, and loses the ground-level `VelocityPlayerToBall` reward it would have collected by just driving. Every one of those attempts looks, to the optimizer, like a mistake.

That is **exploration** — taking an action that looks worse right now because you do not yet know how good it can get. If nothing pushes the policy to keep trying, the maths of policy gradient will do exactly what you told it to: it will make jumping less likely after every failed attempt until the bot never leaves the ground again. It will have found a local optimum — a decent ball-chaser — and it will sit in it for the rest of the run.

Exploration is not a bug you tolerate. It is a resource you spend on purpose.

## Entropy: measuring how undecided the policy is

Your policy outputs a probability for each of the 90 discrete actions in the action table (see [actions.md](./actions.md) for where those 90 come from; here it is just a given). Entropy summarises that whole probability vector in one number: how spread out is it?

```math
H = -\sum_{i=1}^{90} p_i \ln p_i
```

Naming every symbol:

- `H` is the entropy, measured in **nats** because we use the natural logarithm. Multiply by `1/ln 2 ≈ 1.4427` if you want bits instead.
- `p_i` is the probability the policy assigns to action `i` for one specific observation.
- `i` runs over all 90 actions in the table, and the sum covers all of them.
- The minus sign is there because `ln p_i` is negative for any probability below 1, so without it `H` would always be negative.
- By convention, a term with `p_i = 0` contributes 0, not an error. (`p ln p` goes to 0 as `p` goes to 0.)

Two anchors give you the whole scale:

**Maximum entropy.** All 90 actions equally likely, so `p_i = 1/90` for every `i`. Then

```math
H = -\sum_{i=1}^{90} \tfrac{1}{90} \ln \tfrac{1}{90} = -90 \cdot \tfrac{1}{90} \ln \tfrac{1}{90} = -\ln \tfrac{1}{90} = \ln 90 \approx 4.4998 \text{ nats}
```

All 90 terms are identical, so they collapse into one, and `−ln(1/90)` is `ln 90` because flipping the inside of a log flips its sign.

This is a freshly initialised network. It has no opinion about anything.

**Minimum entropy.** One action has probability 1 and the other 89 have probability 0. Then `H = -1 · ln 1 = 0`. The policy is completely certain. Given this observation, it will always do this one thing.

Everything in a real run lives between 0 and 4.50.

A worked middle case. Say the policy has become fairly confident: it puts 0.9 on "boost forward, no steer" and splits the remaining 0.1 evenly across the other 89 actions, so each gets `0.1/89 ≈ 0.0011236`.

```
0.9 * ln(0.9)          = 0.9 * (-0.10536)  = -0.09482
89 * 0.0011236 * ln(0.0011236) = 0.1 * (-6.79123) = -0.67912
sum                                        = -0.77394
H = -(-0.77394)                            =  0.774 nats
```

0.774 nats is low. This policy is nearly committed.

A useful way to read any entropy value: compute `e^H`, the **perplexity**, which is roughly the effective number of actions the policy is still choosing between. `e^4.50 = 90` (all of them). `e^2.0 ≈ 7.4` (about seven live options). `e^0.774 ≈ 2.2` (barely two). Mid-training, an effective 5 to 15 actions is a comfortable place to be.

RL Studio's Entropy panel plots the mean of `H` across every observation in the rollout, so one dot on that graph is the average indecision of the policy over tens of thousands of game states.

## How the entropy bonus enters training

You do not get exploration by adding a separate "explore now" module. You get it by paying for it inside the loss function.

RL Studio's total PPO loss subtracts `ppo.entropy_coef` times the mean entropy. Schematically:

```
loss = policy_loss  +  ppo.vf_coef * value_loss  -  ppo.entropy_coef * H
```

The full policy and value terms are derived in [ppo.md](./ppo.md); the only part that concerns us here is the minus sign in front of the entropy term. The optimizer minimises loss. Subtracting `H` means that raising entropy lowers loss. So the optimizer is being paid, every single gradient step, to keep the action distribution spread out.

Think of it as a standing bribe against premature certainty. It does not tell the bot what to try. It just makes becoming certain slightly expensive, so the policy only commits when the advantage estimates are strong enough to be worth the fee.

## Choosing `ppo.entropy_coef`

**About 0.03 is a working value for this project**, and that is what the default preset ships with.

**Turn it up** (0.1, 0.3): the bot stays scattered. In the viewer you will see it keep trying odd inputs — jumping for no reason, steering away from the ball, air-rolling on the ground — long after those should have been discarded. Learning gets slower because a larger share of every rollout is spent on actions the policy already has evidence against. At extreme values it never converges at all, because you are literally paying more for randomness than the game is paying for goals. The bot ends up a permanently confused generalist.

**Turn it down** (0.001, or 0 exactly): the bot commits early to whatever it found first. In this project, what it finds first is almost always ball-chasing. The reward curve rises for an hour, flattens, and then does nothing for the rest of the night. Nothing is broken; the policy simply stopped generating the variety it needed to discover anything better.

Change it by factors of about three, not by 10%. The difference between 0.03 and 0.033 is not observable; the difference between 0.03 and 0.01 is.

## Entropy collapse

**Entropy collapse** is entropy falling fast and hard toward zero — often from 4.5 to under 0.5 within a few hundred iterations. It is the most common way a training run silently dies.

Causes, in rough order of how often they are actually the culprit:

1. **`ppo.entropy_coef` is too low** for this reward config. The bribe is not covering the cost of staying open-minded.
2. **`ppo.learning_rate` is too high.** Each update moves the probabilities much further than intended, so the policy sprints to certainty before the advantage estimates are trustworthy. Around `1.5e-4` is the reference point for this project.
3. **A reward term with a large weight that pays immediately.** A big touch reward is the classic one: `StrongTouch` at 60 hands out an enormous, unambiguous, instantly-attributable payout for one specific behaviour, and the policy will collapse onto it. Reward design lives in [rewards.md](./rewards.md), but recognise the fingerprint here.
4. **Too many PPO epochs per rollout.** `ppo.epochs` set high means the same batch of experience is re-used many times, and each pass pushes the same direction. The clipping in PPO limits this, but does not eliminate it.

**The symptom in the viewer** is unmistakable once you know it: the bot repeats one behaviour regardless of the situation. It jumps at the ball whether the ball is on the ground, in the air, or behind it. It boosts straight forward off kickoff even when it has already lost the kickoff. The situation changes; the output does not.

**Fix order**, one change at a time so you can tell what worked:

1. Raise `ppo.entropy_coef` (0.03 to 0.1) and restart from a checkpoint taken before the collapse.
2. Halve `ppo.learning_rate`.
3. Look for a reward weight that pays too much, too immediately, and cut it.
4. Reduce `ppo.epochs`.

A collapsed policy does not recover on its own. Once probabilities reach effectively zero, the actions stop being sampled, so they stop generating experience, so they never get credit. Restart from an earlier checkpoint.

## What a healthy entropy curve looks like

It starts near `ln 90 ≈ 4.5`. It drops quickly over the first iterations as the policy discards the obviously useless actions — full reverse while facing the ball, handbrake in mid-air. Then it declines **slowly and smoothly** over hours, and flattens out well above zero. Somewhere in the 1.5 to 3.0 range for a long time is normal and good.

A **flat line at maximum** is also a warning, and it fools people because it looks safe. Entropy that never drops means the policy is not becoming confident about anything. Usually one of two things is true: the reward signal is too weak to distinguish good actions from bad, or `ppo.entropy_coef` is high enough to overwhelm the reward entirely. Check whether mean reward is moving at all. If entropy is pinned at 4.5 and reward is flat, the bot has learned nothing, however calm the graph looks.

## Sampled actions versus deterministic actions

**While training, RL Studio samples the action from the distribution.** It draws action `i` with probability `p_i`. This is not optional. The policy gradient derivation assumes the actions in the rollout were sampled from the current policy — that is what makes the log-probability term valid ([ppo.md](./ppo.md)). And the sampling *is* the exploration: without it, the entropy bonus would be pushing around a distribution that never gets acted on.

**For evaluation, deployment and the viewer's "Best action" mode, RL Studio takes the highest-probability action instead** — `argmax` over the 90 probabilities. No dice roll. Same observation, same action, every time.

Here is the consequence that surprises everyone the first time: **the same checkpoint plays noticeably better and more consistently when acting deterministically.** Under sampling, a policy sitting at entropy 2.0 is choosing among an effective seven-ish options at every decision, fifteen times a second. Most of those alternatives are fine, but a few are bad, and a bad sample at the wrong moment means a whiffed touch or an own-goal-shaped clear. Deterministic play throws all of that away and always takes the mode.

So the training reward curve and your evaluation scores are **not directly comparable**. The training curve is measuring a bot that is deliberately handicapping itself. Do not conclude that a checkpoint regressed because its training reward dipped below an eval score from an hour ago; they are measurements of two different policies derived from the same weights. Compare eval to eval.

In RL Studio: the viewer has a **Sampled / Best action** toggle in its policy panel — flip it while watching and you will see the jitter disappear. The evaluation runner uses `eval.deterministic`, which the CLI exposes as `rl-engine eval --deterministic`, and it defaults to on so that reported scores are reproducible.

## Two misconceptions worth correcting

**"Entropy going down is bad."** No. Entropy going down is the whole point — it means the policy is learning which actions are worth taking. What is bad is entropy going down *fast*, or going all the way to zero. A slow smooth decline is exactly what a healthy run looks like. A flat line at 4.5 means nothing is being learned at all, which is worse than a decline.

**"More exploration is always safer."** No. A high `ppo.entropy_coef` does not merely slow learning down; it can permanently prevent the policy from committing to a good behaviour. The bot finds the aerial, gets rewarded for it, starts to favour it — and the entropy term keeps dragging the probability back toward uniform. What you see is a bot that plateaued at mediocre and never got worse or better. That failure is harder to diagnose than a collapse, because the graphs look calm.

## Other sources of exploration in this project

The entropy bonus is not the only thing generating variety. Every episode begins with `Arena::ResetToRandomKickoff`, so the bot never faces the same starting state twice, and RL Studio's state-reset options can drop it into random mid-play situations it would rarely reach on its own. On top of that, self-play means the opponent's behaviour keeps changing as training proceeds, which keeps presenting the policy with situations its current habits do not cover. See [self-play.md](./self-play.md) for how that works.

## What to look for in RL Studio

- **The Entropy panel** on the training dashboard. It draws a dashed reference line at `ln 90 ≈ 4.50`. A healthy run starts on that line, falls away from it quickly, then decays slowly and levels off well above zero. A near-vertical drop in the first few hundred iterations is collapse — stop the run. A line that stays glued to the reference while mean reward stays flat is the opposite failure.
- **The action-distribution bars** in the viewer, which show the 90 probabilities for the current observation. Watch them as the play develops. Bars that stay broadly spread and shift as the situation changes are healthy. One bar spiking to nearly full height and staying there through kickoff, defence and possession is collapse you can see.
- **The Sampled / Best action toggle** in the viewer. Watch the same checkpoint both ways for thirty seconds. The gap between them is the price you are currently paying for exploration, and it should shrink as entropy declines over a long run.

Next: [training-loop.md](./training-loop.md) walks through one full iteration, from pressing Train to the first point on the graph.
