# PPO: How the Policy Actually Changes

**PPO raises the probability of actions that turned out better than expected, lowers the probability of actions that turned out worse, and refuses to let any one update move the policy far.**

## Why this matters

Everything else in RL Studio produces numbers. The observation builder turns a game state into a vector. The reward function turns an outcome into a score. [GAE](./gae.md) turns those scores into an advantage for every step. None of that changes the bot. PPO is the step that takes those numbers and edits the weights of the network. If you understand this page, you understand what the training run is doing to your bot every few seconds, and you can set `ppo.epochs`, `ppo.minibatch_size`, `ppo.clip_range` and `ppo.learning_rate` on purpose instead of copying them from a forum post.

Everything below is the sentence in bold at the top of this page, written in symbols.

## You cannot differentiate "we scored"

Start with the thing that blocks the obvious approach.

Your network has weights. You want to change the weights so the bot scores more. Gradient descent needs a derivative: how much does the outcome change when this weight changes a little? But the outcome is "the ball crossed the goal line four seconds later." The simulator, the bounce off the wall, the opponent's block — none of that is a differentiable function of your weights. You cannot take `d(goal)/d(weight)`.

Here is what you *can* differentiate. The policy is a network that reads an observation and outputs a probability for each of the 90 actions in the [action table](./actions.md). On one particular step the bot was at midfield, the network gave "boost forward, steer slightly left" a probability of 0.04, and the sampler drew it. During training RL Studio rolls a weighted die over all 90 actions rather than taking the most likely one, which is why a 0.04 action gets played at all (see [Exploration and Entropy](./exploration.md)). The number 0.04 *is* a differentiable function of the weights. You can compute exactly how each weight would have to move to make that number 0.05 instead.

So the trick is: do not try to differentiate the outcome. Use the outcome only as a *weight* on the gradient you can actually compute. If that action turned out well, push its probability up. If it turned out badly, push it down. That is the policy gradient.

The vanilla policy gradient objective:

```math
L^{PG}(\theta) \;=\; \hat{\mathbb{E}}_t\!\left[\, \log \pi_\theta(a_t \mid s_t)\; \hat{A}_t \,\right]
```

Every symbol:

- **θ** — the weights of the policy network. These are what training changes.
- **π_θ** — the policy: the function from an observation to a probability for each action.
- **s_t** — the observation at step *t*, the vector described in [What the Bot Sees](./observations.md).
- **a_t** — the action the bot actually took at step *t*, drawn from π_θ itself. This objective is **on-policy**: it is only a valid estimate while the weights are still the ones that chose the action. The next section is about what happens when they are not.
- **π_θ(a_t | s_t)** — the probability the current network assigns to that action in that state.
- **Â_t** — the advantage of that action: how much better the outcome was than what the value head predicted for that state. Positive means "better than expected." [GAE](./gae.md) computes it; [Policies, Values and Advantage](./policy-and-value.md) defines it. Treat it as given here.
- **Ê_t** — the average over all the steps in the rollout.

Why a logarithm, rather than the probability itself? Because what matters is *relative* change. Moving a probability from 0.02 to 0.04 is the same size of edit to the policy as moving 0.40 to 0.80, but the raw differences are 0.02 and 0.40. The derivative of `log p` is `1/p` times the derivative of `p`, and that `1/p` divides out the starting size, leaving percentage change. A log also turns the product of probabilities along a trajectory into a sum, which is far kinder numerically. RL Studio stores log-probabilities, not probabilities, in the rollout buffer for the same reason.

Take the gradient of that with respect to θ and you get `∇log π_θ(a_t|s_t) · Â_t`.

- **∇** (nabla) — the gradient: the list of partial derivatives of the expression with respect to every weight in θ. It points in the direction in weight-space that increases the expression fastest.

The gradient of the log-probability points in the direction that makes this action more likely. Multiplying by the advantage scales it and, when the advantage is negative, flips it around so the update makes the action *less* likely.

Concretely: the bot drove past the ball at full boost and gave up possession. The advantage for that step is negative. The update nudges every weight in the direction that lowers the probability of "boost forward" from that observation.

## Why the plain version wastes your data

There is a catch buried in `Ê_t`. The expectation is over states and actions drawn from the *current* policy.

A **rollout** is one batch of experience. RL Studio freezes the network weights, lets every arena play for a while, and stores each decision — observation, action, log-probability, reward, value estimate — in the **rollout buffer**. [The training loop](./training-loop.md) walks through the phases. So your rollout was collected by the policy as it was *before* the update. The moment you apply one gradient step, the network is no longer the one that produced the data, and the data is no longer a valid sample of the thing you are averaging.

So the vanilla policy gradient gets one update per rollout, then the data has to be thrown away. A rollout in RL Studio is 50,000 to 100,000 steps. Spending several seconds of simulation across hundreds of arenas to earn a single gradient step is very slow.

PPO's whole reason to exist is to safely take many gradient steps on one rollout.

## The importance ratio

```math
r_t(\theta) \;=\; \frac{\pi_\theta(a_t \mid s_t)}{\pi_{\theta_{\text{old}}}(a_t \mid s_t)}
```

- **π_θ(a_t | s_t)** — the probability the network *right now* gives to the action that was actually taken.
- **π_θ_old(a_t | s_t)** — the probability the network gave that same action *when it collected the data*. RL Studio stores this number alongside every step in the rollout buffer, so it is a fixed constant during the update.
- **r_t(θ)** — the ratio of the two.

Read it as "how much more likely is this action now than it was then." A ratio of 1.0 means nothing has changed for this action. 1.3 means the action is now 30 percent more likely than when it was sampled. 0.7 means 30 percent less likely.

The ratio is also the correction factor that makes old data usable: `r_t · Â_t` is an estimate of how the current policy would score on data the old policy collected. That correction is trustworthy while the ratio stays near 1, and increasingly wrong as it drifts away.

## The clipped surrogate objective

It is called a *surrogate* because it stands in for the thing we actually want. Nobody cares about the value of this expression. It is not a score, and a lower number is not a better bot. It is built so that its *gradient* is the update we want. That is why Policy Loss on the dashboard hovers around zero and its sign means nothing.

```math
L^{CLIP}(\theta) \;=\; \hat{\mathbb{E}}_t\!\left[\, \min\!\Big( r_t(\theta)\,\hat{A}_t,\;\; \text{clip}\big(r_t(\theta),\, 1-\epsilon,\, 1+\epsilon\big)\,\hat{A}_t \Big) \right]
```

Line by line:

- **r_t(θ) Â_t** — the unclipped term. This is the plain policy gradient with the old-data correction applied.
- **clip(r_t(θ), 1−ε, 1+ε)** — the same ratio, clamped so it can never be reported as less than 1−ε or more than 1+ε.
- **ε** — the clip range. In RL Studio this is `ppo.clip_range`, default about 0.2, so the ratio is clamped to [0.8, 1.2].
- **min(...)** — take whichever of the two terms is smaller. This is the pessimistic choice, and it is what makes the clipping bite.

**Turning `ppo.clip_range` down** (0.1) tightens the brake. Each update phase moves the policy less and Approx KL falls — but Clip Fraction goes *up*, not down, because a narrower band leaves more samples outside it. Use it when updates are overshooting and you have already cut epochs. **Turning it up** (0.3) loosens the brake: the policy can move further per rollout and Clip Fraction falls, at the cost of the protection this whole page is about. Much above 0.3 you have given away most of the safety, and the run starts behaving like the vanilla policy gradient with recycled data.

Work through a positive-advantage case. The bot flicked the ball off the wall and it went in; that step has Â_t = +2. Suppose the update has already pushed the ratio to 1.5. The unclipped term is 1.5 × 2 = 3.0. The clipped term is 1.2 × 2 = 2.4. The minimum is 2.4, which does not depend on θ any more — its gradient is zero. Past a ratio of 1.2, making that action even more likely earns the objective nothing, so the update stops pushing on it.

Now the negative-advantage case. The bot whiffed and Â_t = −2. The update pushes the ratio down. At a ratio of 0.5 the unclipped term is 0.5 × −2 = −1.0 and the clipped term is 0.8 × −2 = −1.6. The minimum is −1.6, again constant, again zero gradient. Below 1 − ε there is no further gain from suppressing the action.

The asymmetry worth noticing: the clip only removes the incentive to keep moving in the direction the objective likes. If the ratio moves the *wrong* way — a good action becoming less likely, or a bad action becoming more likely — the unclipped term is the smaller one, so it is the one that survives the `min`, and the gradient still pulls the ratio back. Clipping is a one-way brake, not a wall.

## What clipping actually buys you, in Rocket League terms

Imagine one rollout where the bot got lucky. Three shots deflected off an opponent and went in. Those steps carry large positive advantages. Without clipping, several epochs of gradient descent on that data will hammer the probability of those exact steering-and-boost actions upward until they dominate the policy in states that look vaguely similar. The bot comes out of the update no longer able to drive to the ball cleanly.

The worst part is that nothing in the data warns you. It was collected by the old policy, so it contains no evidence at all about how the new, over-corrected policy behaves.

Clipping is a cheap **trust region** — a region around the old policy inside which you are still willing to trust your data. The expensive version, used by PPO's predecessor TRPO, solves a constrained optimisation problem on every update to keep the new policy within a measured distance of the old one. PPO throws that machinery away and gets most of the benefit from one `min` and one `clip`. The rule is the same either way: do not move far from the policy that generated this data, because past that distance the data stops being evidence.

## Epochs and minibatches

After a rollout is collected, PPO does not take one step and stop. It passes over the same buffer `ppo.epochs` times. Each pass shuffles the steps and splits them into chunks of `ppo.minibatch_size`, and each chunk gets one gradient step.

The trade-off runs in both directions.

- **More epochs** squeeze more learning out of each expensive rollout. They also carry the policy further from the data that justifies the update. You see this as rising Approx KL and a rising Clip Fraction — by the last epoch a large share of samples sit outside the clip band. For the ones that drifted in the direction the objective was already pushing, the `min` picks the clipped term and the gradient is zero; that part of the batch has stopped doing anything. For the ones that drifted the wrong way, the unclipped term survives and still pulls at full strength, exactly as the one-way brake above describes. So a high Clip Fraction means a large part of the batch is no longer buying you the movement you paid the rollout for, and the samples that still move you may be the ones you should trust least.
- **Fewer epochs** are safer and slower. Every rollout costs you real simulation time, and one epoch throws most of that value away.

A reasonable starting point for a rollout of 50k–100k steps is `ppo.epochs = 2` and `ppo.minibatch_size` around a quarter of the rollout. Treat that as a starting point, not a law; the right value depends on your reward scale and learning rate. When Approx KL blows up, the first move is to cut epochs, then lower `ppo.learning_rate`, then narrow `ppo.clip_range`. Change one at a time.

Smaller minibatches mean more gradient steps per epoch, which moves the policy further per epoch and is noisier per step. Larger minibatches give a cleaner gradient estimate and fewer, larger steps.

## The actor-critic split

RL Studio's network is one trunk with two heads. One head outputs the action probabilities — the policy, the actor. The other outputs a single number, the value estimate for that state — the critic. [Policies, Values and Advantage](./policy-and-value.md) explains what each one means.

They share the early layers for two reasons. Both heads need the same understanding of the game state: where the ball is going, who is closest, whether the bot has boost. And sharing roughly halves the compute compared to two separate networks.

The risk is that the two objectives fight. The value head wants representations good for predicting returns; the policy head wants representations good for choosing actions. A value loss that dwarfs the policy loss will drag the shared trunk toward being a good predictor and a bad player. `ppo.value_coef` is the dial that balances them.

Layer sizes, activations and normalization belong to [training-loop.md](./training-loop.md).

## The total loss

RL Studio minimises:

```math
L(\theta) \;=\; -\,L^{CLIP}(\theta) \;+\; c_v \cdot L^{V}(\theta) \;-\; c_e \cdot H\big[\pi_\theta\big]
```

- **−L^CLIP(θ)** — the clipped surrogate from above, negated. We maximise the surrogate, and optimisers minimise, so it enters with a minus sign.
- **c_v** — `ppo.value_coef`, the weight on the value loss. Turn it up and the critic learns faster at the cost of pulling the shared trunk toward its own objective. Turn it down and advantage estimates stay noisy for longer.
- **L^V(θ)** — the value loss: the squared error between the value head's prediction for a state and the return target computed from the rollout, which is the advantage plus the old value estimate. [GAE](./gae.md) owns that target.
- **c_e** — `ppo.entropy_coef`, the weight on the entropy bonus.
- **H[π_θ]** — the entropy of the policy, a measure of how spread out its action probabilities are. It enters with a minus sign because higher entropy lowers the loss, which keeps the bot trying things. [Exploration and Entropy](./exploration.md) owns this term in full.

## Optimizer settings

- **`ppo.learning_rate`** — how large a step the optimizer takes per gradient. About `1.5e-4` is a working value for this project. Too high shows up as entropy collapsing toward zero within a few hundred iterations and the value loss spiking. Too low and the reward curve is flat but healthy-looking, which wastes hours.
- **`ppo.max_grad_norm`** — before the optimizer step, the total length of the gradient vector across all weights is measured, and if it exceeds this value the whole gradient is scaled down to that length. It is a safety net: one minibatch with a freak advantage cannot produce one enormous step. It does not change the direction of the update, only its size. Lowering it makes training slower and steadier.
- **`ppo.target_kl`** — an optional early stop. After each epoch, RL Studio estimates the KL divergence between the old policy and the current one from the stored log-probabilities. If it exceeds `ppo.target_kl`, the remaining epochs are abandoned and the rollout is discarded. Something around 0.02 is a common ceiling for this scale of run. Set it and you get an automatic brake; leave it off and a bad rollout can be over-used before you notice.

## A misconception to correct

**The misconception:** "Clipping caps how much the reward can change the policy."

It does not. Clipping caps the *ratio*, which caps how far the probability of an already-taken action can move **within one update phase, for that action, relative to where it started**. Three things it does not do:

1. It does not bound total movement across iterations. Every iteration resets θ_old to the current policy, so the trust region re-centres on wherever you now are. Twenty iterations at the clip boundary walk the policy a long way.
2. It does not protect you from a bad reward function. If `VelocityPlayerToBall` outweighs everything else, PPO will patiently and safely walk the policy toward a bot that rams the ball and never rotates. Each step is small and legal. The destination is still wrong. See [Designing the Reward](./rewards.md).
3. It does not bound the change in probability for actions that were *not* sampled. The ratio is only defined for the action in the buffer. Renormalisation moves the others as a side effect.

Clipping keeps each update honest with respect to its data. It does not make the training run correct.

## What to look for in RL Studio

Three panels on the dashboard tell you whether the update phase is behaving. [Reading the Training Graphs](./reading-the-graphs.md) covers full triage.

- **Policy Loss** — the negated clipped surrogate. It hovers near zero and is noisy, and its sign is not a quality score. Watch it for sudden magnitude jumps, not for a trend.
- **Approx KL** — how far the policy moved during the update, estimated from the stored log-probabilities. Should sit in the low thousandths to about 0.02 and stay stable. A spike means the update phase overshot: cut `ppo.epochs` or `ppo.learning_rate`.
- **Clip Fraction** — the fraction of samples whose ratio landed outside [1−ε, 1+ε]. This is the single most direct readout of this page. Near zero means the updates are too timid: the policy is barely moving, and you can afford more epochs or a higher learning rate. Far above roughly 0.3 means most of the batch is clipped, the rollout is being over-used, and you should cut `ppo.epochs` first.
