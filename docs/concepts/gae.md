# GAE: Estimating the Advantage

**Generalized Advantage Estimation is a dial that decides how much of the bot's future you use when you score one decision: one step ahead trusts the critic, the whole episode trusts the rewards, and lambda picks a blend.**

## Why this matters

[PPO](./ppo.md) needs a number for every step in the rollout: was this action better or worse than what the critic expected from that state? That number is the advantage. [Policies, Values and Advantage](./policy-and-value.md) defines what an advantage *is*. This page is about how RL Studio actually computes one, because you never get to observe the true advantage. You only get rewards and a critic that is still learning.

The estimator you choose changes training more than most people expect. Get it wrong and the bot learns confident nonsense from an estimate that is either mostly critic error or mostly noise from events the action had nothing to do with. Two settings and one flag control this, and one of them is the source of a bug that ships in a large fraction of hand-written PPO implementations.

## The two honest ways to score an action, and why both are bad

Say the bot makes a clean clearance off its own backboard at step 40 of an episode.

**Option A: look one step ahead.** Take the reward for that step, add the critic's opinion of the state you landed in, and compare against the critic's opinion of the state you left. This is cheap and quiet. It has one problem: almost all of it is the critic's guess. Twenty minutes into a run, the critic is badly wrong about most states. You are scoring a real action with an estimate built out of guesswork. That is **bias** — the estimate is systematically off in whatever direction the critic is currently wrong.

The move Option A makes has a name. Standing in for the rest of the episode with the critic's own guess about the next state is called **bootstrapping** — you are pulling yourself up by your own estimate. The `γV(s_{t+1})` term is the bootstrap. Remember the word. The "Termination versus truncation" section near the end of this page is entirely about when you are allowed to bootstrap and when you must set that term to zero.

**Option B: look at the whole rest of the episode.** Add up every discounted reward from step 40 to the end and subtract the critic's baseline. The forward-looking half of this is not a guess — every one of those rewards actually happened. You do still subtract the critic's `V(s_t)`, which is a guess. But that subtraction is safe in a way the bootstrap in Option A is not: `V(s_t)` does not depend on which action you took, so on average it cancels out of the gradient rather than tilting it. That sounds better until you notice what "actually happened" includes. The clearance was good. Eight seconds later the bot's teammate own-goals. The full-episode return charges that own-goal to the clearance. Do that a few thousand times across a batch and the signal for "clearance" is buried under everything that happened afterwards. That is **variance** — the estimate is right on average but swings wildly from sample to sample.

Neither end is right. A biased estimate points the policy in a slightly wrong direction very consistently. A high-variance estimate points in the right direction on average but so noisily that you need enormous batches to see it. GAE gives you a knob between them.

## The TD error: one step of surprise

Everything is built from one quantity.

```math
\delta_t = r_t + \gamma V(s_{t+1}) - V(s_t)
```

- `δ_t` (delta) — the **temporal-difference error** at step `t`. One number per step.
- `r_t` — the reward the bot collected on that step, summed over the ticks in the step (see [Designing the Reward](./rewards.md)).
- `γ` (gamma) — the discount factor, the setting **`ppo.gamma`**. How much a reward one step later is worth compared to now. Turn it up and the bot weighs distant consequences more heavily; turn it down and it becomes greedy about the next second or two. What that means in seconds of Rocket League time is covered in [Policies, Values and Advantage](./policy-and-value.md).
- `V(s_t)` — the critic's estimate of how much total future reward the state at step `t` is worth.
- `V(s_{t+1})` — the critic's estimate for the state the bot landed in.

In one sentence: **the TD error is the surprise.** It is how much better the step turned out than the critic expected, where "turned out" means the reward you actually got plus the critic's revised opinion of where you now are.

Work one by hand. Gamma is 0.99. The bot is at midfield with the ball loose; the critic says this state is worth `V(s_t) = 0.40`. The bot flicks the ball toward the opponent's net. The shaped reward for that step is `r_t = 0.15`. The bot is now in a state the critic likes a lot: `V(s_{t+1}) = 0.90`.

```
delta = 0.15 + 0.99 * 0.90 - 0.40
      = 0.15 + 0.891 - 0.40
      = 0.641
```

Positive, and large. That step went much better than the critic saw coming. Now the whiff: same start state at 0.40, the bot drives past the ball, reward `-0.05`, and the state it lands in is worth 0.10.

```
delta = -0.05 + 0.99 * 0.10 - 0.40
      = -0.05 + 0.099 - 0.40
      = -0.351
```

Negative. PPO will push down the probability of that action in that state.

## From one step to n steps to GAE

A single `δ_t` is Option A above. You can look two steps ahead instead, or three, or `n`, trusting the real rewards for `n` steps and the critic only at the end. Larger `n` means less bias and more variance. Somewhere there is a good `n`, but it is a different `n` for every situation and you would have to tune it.

GAE does not pick an `n`. It takes *every* `n` at once and averages them with exponentially decaying weights.

Why deltas and not raw rewards? Because the deltas already do the subtraction for you. Add the first two together and watch the middle terms cancel:

```
  delta_t + gamma * delta_{t+1}
= [r_t     + gamma*V(s_{t+1}) - V(s_t)]
+ gamma * [r_{t+1} + gamma*V(s_{t+2}) - V(s_{t+1})]
= r_t + gamma*r_{t+1} + gamma^2 * V(s_{t+2}) - V(s_t)
```

The `+gamma*V(s_{t+1})` on the first line and the `-gamma*V(s_{t+1})` on the second destroy each other. What survives is two real rewards, the critic taking over from step `t+2`, and the baseline subtracted once. That is exactly the two-step advantage. The same cancellation runs all the way down, which is why the general `n`-step estimate is a discounted sum of deltas:

```math
A_t^{(n)} = \delta_t + \gamma \delta_{t+1} + \dots + \gamma^{n-1} \delta_{t+n-1}
```

- `A_t^(n)` — the `n`-step advantage for step `t`: trust the real rewards for `n` steps, then let the critic take over.
- `δ_{t+k}` — the TD error `k` steps after step `t`.
- `γ` — the same discount factor as before.

Now average all of those estimates together, giving the `n`-step estimate a weight proportional to `λ^(n-1)`:

```math
A_t = (1-\lambda)\left[ A_t^{(1)} + \lambda A_t^{(2)} + \lambda^2 A_t^{(3)} + \dots \right]
```

- `λ` (lambda) — the decay knob, between 0 and 1. This is the whole point of the page.
- The `(1-λ)` out front is there to make the weights sum to 1, so this is an average and not just a sum.

Multiply that out, collect the terms that share a delta, and you get the form RL Studio actually uses:

```math
A_t = \sum_{l=0}^{\infty} (\gamma \lambda)^l \, \delta_{t+l}
```

- `A_t` — the advantage estimate for step `t`, the number PPO consumes.
- `l` — how many steps into the future you have walked.
- `δ_{t+l}` — the TD error `l` steps later.
- `γ`, `λ` — as above.

You do not need to do that algebra yourself; it is worked in Schulman et al. 2015, *High-Dimensional Continuous Control Using Generalized Advantage Estimation*. What you should be able to see is that the weighted average and the single sum are the same object, written two ways.

Nobody computes that sum forward. Written backwards it collapses to one line:

```math
A_t = \delta_t + \gamma \lambda A_{t+1}
```

RL Studio walks each rollout backwards from its last step, carrying one running number per environment. That is the entire implementation. It is O(1) work per step and it is where the bugs hide.

A three-step example. Gamma 0.99, lambda 0.95, so `γλ = 0.9405`. The TD errors came out `δ₀ = 0.20`, `δ₁ = -0.10`, `δ₂ = 0.60`, and the carry starts at zero past the end.

```
A2 = 0.60
A1 = -0.10 + 0.9405 * 0.60   = 0.4643
A0 =  0.20 + 0.9405 * 0.4643 = 0.6367
```

Step 0 surprised the critic only mildly on its own. Two steps later something happened that the critic had not priced in at all, and that surprise flows backwards, so step 0 ends up with a strongly positive advantage. Note what this does and does not claim: GAE does not know that step 0 caused the good thing. It assumes that surprises close together in time are related. Lambda is how strongly you are willing to assume that.

## What lambda actually does

**At `λ = 0`** the sum has one term and `A_t = δ_t`. This is the pure one-step estimate. Lowest variance you can get, and the most reliant on the critic — the estimate leans on `V(s_{t+1})`, a guess, for everything past the current step. Most biased.

**At `λ = 1`** the `(γλ)^l` weights become `γ^l`, and every `V(s_{t+1})`, `V(s_{t+2})` and so on cancels against its neighbour exactly as it did in the two-step case above — each one appears once with a plus and once with a minus. What is left is `A_t` = the full discounted return from step `t` minus `V(s_t)`. Unbiased. Highest variance.

Unbiased is worth being careful about here. The estimate still contains `V(s_t)`, which is a guess, and the guess can be badly wrong. But `V(s_t)` does not depend on which action the bot took, so across a batch it cancels out of the gradient instead of tilting it. That is the sense in which lambda = 1 is unbiased even when the critic is bad. This is the own-goal-blames-the-clearance case.

The setting is **`ppo.gae_lambda`**. A working default is **0.95**. Turn it down toward 0.9 and each advantage is built from fewer real rewards and more critic opinion; the numbers get calmer and the policy takes smoother, more correlated steps. Turn it up toward 0.99 and each advantage is built from more of what really happened; the numbers get noisier but they stop inheriting whatever the critic is currently wrong about.

Note the direction carefully, because it is easy to get backwards: **higher lambda leans harder on the actual rewards; lower lambda leans harder on the critic.** So when the critic is bad — early in a run, or right after you change reward weights, or whenever Explained Variance is low — raising lambda is usually the safer move, not lowering it. A bad critic is exactly the thing you do not want the estimate resting on. The cost is noisier gradients, which you pay for with a larger batch or a smaller learning rate. Lowering lambda is the move when the critic is already good and the gradients are too noisy to make progress.

One limit on that advice: raising lambda protects the *policy* from a bad critic, but it does not repair the critic, and as the next section shows it also changes what the critic is trained on. The Explained Variance notes at the end of this page cover what actually fixes a stalled critic.

## The value target, and where it comes from

The critic also needs something to train toward. RL Studio does not compute returns separately. It reuses the advantages:

```
returns = advantages + values_predicted_during_collection
```

Where `values_predicted_during_collection` are the `V(s_t)` the critic produced while the rollout was being gathered — the exact same numbers used to build the TD errors, not fresh forward passes. The critic is then fit to `returns` with a squared-error loss.

Rearranging the definition of advantage gives `target = A_t + V(s_t)`, where `A_t` is the GAE advantage from this page and `V(s_t)` is the critic's estimate stored at collection time.

It is worth being precise about what that target actually is. At `λ = 1` it is exactly the discounted return `G_t` defined in [Policies, Values and Advantage](./policy-and-value.md). At `λ` below 1 it is the **lambda-return**: the same exponentially weighted blend of short- and long-horizon estimates that built the advantage. So `ppo.gae_lambda` moves the critic's training target too, not only the actor's signal. A lower lambda gives the critic a smoother target built more from its own current guesses — easier to fit, which does not by itself mean the critic understands the future any better. A higher lambda gives it a noisier target built from more real reward.

The identity is worth stating explicitly because it is where implementation bugs live. If you recompute values after the network has been updated and add those to the advantages, your targets drift with the network you are training and the critic chases its own tail. Store the values at collection time. Use those.

## Advantage normalization

Before the PPO update, RL Studio takes the advantages across the batch, subtracts their mean, and divides by their standard deviation.

```
advantages = (advantages - advantages.mean()) / (advantages.std() + 1e-8)
```

**What this fixes:** the size of the policy gradient stops depending on the scale of your rewards. Without it, doubling the Goal reward weight roughly doubles your advantages, which roughly doubles the effective step size on the policy — you changed the reward design and silently changed the learning rate too. With normalization, the update sees a distribution centered on 0 with spread 1 no matter what the raw reward numbers look like.

**What it costs:** the absolute goodness of the rollout is thrown away. What survives is each advantage's position relative to the batch mean, measured in batch standard deviations. The gaps between advantages keep their proportions — an action twice as far above the mean as another still is. The common offset and scale do not survive. If every action in a batch was genuinely terrible, normalization still makes the least terrible ones look positive and pushes the policy toward them. That is usually fine and occasionally not, which is why it is a switch: **`ppo.normalize_advantages`**. Leave it on unless you have a specific reason.

## Termination versus truncation

This is the section to read twice. [What Reinforcement Learning Is](./what-is-rl.md) promised that these two endings look identical in the loop but must be treated differently by the mathematics. Here is the difference.

An episode can end for two completely different reasons, and they call for opposite treatment.

**A termination is a real end of the world.** A goal is scored. The episode is genuinely over. There is no step 41. There is no future reward from that state, ever. The bootstrap value is **0**:

```
delta_T = r_T + gamma * 0 - V(s_T)
```

**A truncation is the trainer cutting the episode off for its own reasons.** `env.max_episode_seconds` expired. The arena is being reset to a fresh kickoff. Nothing about the *game* ended — the ball was still rolling, the bot was still mid-play, and had the trainer not intervened there would have been a step 41 with real reward in it. The value target must bootstrap from the critic's estimate of the final state:

```
delta_T = r_T + gamma * V(s_{T+1}) - V(s_T)
```

Where `s_{T+1}` is the observation that came *after* the final action — the state the arena had actually reached one step later, before the reset wiped it. That state was real. The bot was standing in it. The trainer just refused to let it continue.

This observation has to be captured deliberately at the moment of the cut, because the arena auto-resets. Whatever sits in the next-observation slot of the buffer is the kickoff of a brand new episode, worth something completely unrelated. Bootstrapping from that is its own separate bug.

Separately from the bootstrap, the backward recursion's carry `A_{t+1}` is zeroed at **both** kinds of boundary. You can never propagate advantage from one episode back into the previous one.

RL Studio carries two independent flags per step. `terminated` means the world genuinely ended. `truncated` means the trainer cut a still-live episode off. An episode boundary is `terminated or truncated` — that OR is what older APIs call `done`. If your wrapper only hands you `done`, recover the flag the bootstrap needs with `terminated = done and not truncated`. Never feed `done` in where the code expects `terminated`. A goal is `terminated=true, truncated=false`. A time limit is `terminated=false, truncated=true`.

In code the two flags do different jobs:

```
# terminated -> the world really ended:        bootstrap from 0
# truncated  -> we cut a live episode short:   bootstrap from the value stored at the cut
# either one -> a new episode starts:          zero the carry A_{t+1}

carry = 0.0
for t in reversed(range(T)):
    if terminated[t]:
        next_value = 0.0
    elif truncated[t]:
        next_value = V_at_cut[t]   # stored deliberately. NOT V[t+1]:
                                   # V[t+1] is a fresh kickoff from a new episode.
    else:
        next_value = V[t + 1]      # for the final step of the buffer this comes from the
                                   # extra forward pass described in the note below.

    if terminated[t] or truncated[t]:
        carry = 0.0

    delta = r[t] + gamma * next_value - V[t]
    A[t]  = delta + gamma * lam * carry
    carry = A[t]
```

`V_at_cut` is a separate array. It is not `V[t+1]`. Filling it requires the environment wrapper to hand back the pre-reset final observation, which is why the rollout buffer stores that observation as its own field.

There is a third kind of boundary, and it is the one you hit most often. It is neither a goal nor a time limit. The rollout buffer fills up on a fixed schedule, and it almost never fills exactly when an episode ends. With 256 arenas each advancing about 98 steps per iteration (see [The Training Loop](./training-loop.md)), nearly every arena is mid-play when collection stops. Treat that cut the same way you treat a time limit: the carry starts at zero, and the bootstrap is the critic's value for the observation the arena was left sitting in. RL Studio runs one extra forward pass at the end of collection to get those values. The bot did not stop existing because the buffer got full.

### The bug that follows from conflating them

If you treat a time-limit cut-off as a termination, you set the bootstrap to zero on states that were not actually worthless. Take a concrete limit — say `env.max_episode_seconds = 30`. You are then telling the critic, thousands of times per iteration, that every state near the 30-second mark has zero future value.

The critic believes you. But because RL Studio's observation carries no clock — see [What the Bot Sees](./observations.md) — the critic cannot learn "the world ends at 30 seconds" as a time-indexed rule. It has no input that tells it how far into the episode it is. What it does instead is worse to diagnose. The false zeros land on ordinary mid-play states, so the critic learns that ordinary states are worth less than they are. Values get dragged down across the board. The bias is largest for exactly the long-horizon situations `ppo.gamma` was chosen to cover, and the advantages built on those values are wrong in the same direction.

The symptom is a critic that will not improve: Explained Variance stalls low and Value Loss plateaus high, while the reward curve still creeps up. That combination is why you can lose a week to this. If you do add a time-remaining feature to the observation, the bug additionally becomes learnable as a clock-linked sag, and then you get the behavior people describe — the bot stops committing near the cut-off, refusing to start a boost-heavy rotation at 24 seconds because the payoff for arriving at 32 seconds has been erased.

This is one of the most common bugs in hand-written PPO implementations, and it is quiet — training does not crash. RL Studio carries `terminated` and `truncated` separately through the entire rollout buffer for exactly this reason. If you write your own environment wrapper, set both flags honestly.

## What to look for in RL Studio

**Explained Variance** is the panel that tells you whether the critic is any good, and therefore whether your advantages mean anything:

```math
\text{EV} = 1 - \frac{\mathrm{Var}(\text{returns} - \text{values})}{\mathrm{Var}(\text{returns})}
```

Read it as the fraction of the variation in the **value targets** that the critic predicted. Those targets are the ones built on this page — advantages plus collection-time values — not realized end-of-episode returns. That distinction matters: at low `ppo.gae_lambda` the targets are heavily bootstrapped from the critic's own predictions, so EV measures short-horizon consistency more than long-horizon accuracy. Compare EV across runs only when lambda is the same.

0 means the critic is no better than always guessing the mean. 1 means it is perfect. Negative means it is worse than guessing the mean, which happens for the first few iterations and should not persist. Healthy training climbs off zero within the first few dozen iterations and settles somewhere in the **0.4 to 0.9** band after a few hundred. The exact number depends on how stochastic your reward is — see [Reading the Training Graphs](./reading-the-graphs.md) for the full band and what pulls it down.

If it stalls low, your advantages are mostly critic error. Rule out the causes before chasing the symptom. Check that `truncated` is being set and that the truncation bootstrap uses the stored final observation. Check that the value targets are built from collection-time values. Check that the reward scale has not changed under the critic. If all three are clean, raise **`ppo.value_coef`** — that gives the critic more of the shared trunk, and it is the first knob to reach for. Raising `ppo.gae_lambda` is a different move: it makes the *policy* lean less on the bad critic, which is worth doing, but it also makes the critic's own target noisier, so EV itself may not improve and can dip. Judge that change by the reward and ELO curves, not by the EV panel it partly redefines.

**Value Loss** should fall and then plateau at a non-zero level. It never reaches zero, because returns are genuinely stochastic. A value loss that climbs steadily while reward is flat usually means the reward scale changed or the targets are being built from post-update values.

**The advantage mean/std readout**, shown after normalization, should sit near **0** and **1**. If it does not, normalization is not running or is being applied per-minibatch when you expected per-batch.

If you want the full checklist for reading these panels together, see [Reading the Training Graphs](./reading-the-graphs.md).
