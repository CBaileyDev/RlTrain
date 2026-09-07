# Policies, Values and Advantage

**The policy is the bot's habits — what it presses when it sees something — and the value function is the bot's opinion about how well things are going, which RL Studio uses to work out whether each individual decision was better or worse than the bot's own average.**

## Why this matters

In [What Reinforcement Learning Is](./what-is-rl.md) you saw the loop: the bot observes, acts, gets a reward, observes again. That page never said *how* the bot decides. This page does. It also introduces the second network RL Studio trains, the one that never touches the controller — the critic. Almost every number on the training dashboard is about one of these two networks. If you understand the policy, the value function and the advantage, you can read the dashboard instead of squinting at it. And every later page leans on the word "advantage": [GAE](./gae.md) estimates it, [PPO](./ppo.md) multiplies by it. This is the page where it gets defined.

## The policy: the bot's habits

The policy is the thing that plays. Hand it what the bot sees and it gives you what to press. That is all it is. In RL Studio the policy is a neural network; its input is the observation vector described in [What the Bot Sees](./observations.md) and its output describes a controller input.

The standard notation is:

```math
\pi(a \mid s)
```

Read aloud: "the probability of taking action **a** when the observation is **s**." Naming each symbol:

- **π** (pi) is the policy itself — the network and its weights.
- **a** is one action, one entry from RL Studio's action table (see [What the Bot Can Do](./actions.md)).
- **s** is the observation the bot is looking at right now — the vector of numbers describing the ball, the cars and the boost.

One caveat before we build on that letter. Strictly, the textbook definitions of π, V and Q on this page are written over the game's full **state** — everything the simulator is tracking. RL Studio hands the network an *observation* instead, and the theory carries over only as far as that observation captures what matters about the future. Where it does not, two situations that look identical to the bot can have genuinely different futures, and no amount of training will let the critic tell them apart. [What Reinforcement Learning Is](./what-is-rl.md) covers the Markov property behind this; [What the Bot Sees](./observations.md) lists what is actually in the vector.

Training changes π. Nothing else about the bot changes. When people say "the bot got better", they mean the numbers inside π moved.

## Why the policy outputs probabilities, not a choice

A *deterministic* policy would map each observation to exactly one action: see this, press that. RL Studio's policy is *stochastic* instead. Its final layer produces one raw score per action in the discrete action table — 90 of them. Those raw scores are called **logits**: unnormalized numbers, not probabilities. A softmax turns the 90 logits into 90 probabilities that sum to 1. That is a **categorical distribution**, and during training RL Studio samples from it: an action with probability 0.30 is chosen about 30% of the time. [The Training Loop](./training-loop.md) shows the network shape that produces them.

Two reasons this matters.

**It makes policy-gradient learning possible.** You cannot take a derivative of "the bot chose action 47." That is a discrete jump, and a gradient needs something that varies smoothly. But π(a | s) — *how often* action 47 gets chosen — is a smooth function of the network weights. Nudge a weight, the probability moves a little. That is the hook the whole learning algorithm hangs on, and [PPO](./ppo.md) shows how it is used. (Other families of RL algorithm get around this differently — value-based methods like DQN learn a value for every action and then take the best one, with no action probabilities anywhere. RL Studio uses PPO, which is a policy-gradient method, so it needs the probabilities.)

**It makes trying things possible.** A deterministic bot that has learned to drive forward will drive forward forever and never discover that jumping into the ball scores. Sampling means the bot sometimes does something other than its favorite. How much other, and how to keep that from collapsing, belongs to [Exploration and Entropy](./exploration.md).

## Return: the number the bot is actually chasing

The bot is not trying to maximize the reward it gets right now. It is trying to maximize everything that comes after. That total is the **return**:

```math
G_t = r_t + \gamma r_{t+1} + \gamma^2 r_{t+2} + \dots = \sum_{k=0}^{\infty} \gamma^k \, r_{t+k}
```

- **G_t** is the return from step *t* — the whole discounted pile of future reward, seen from step *t*.
- **r_t** is the reward at step *t*, whatever the reward function in [Designing the Reward](./rewards.md) hands back.
- **γ** (gamma) is the discount factor, a number between 0 and 1.
- **k** counts steps into the future: k = 0 is now, k = 1 is the next decision, and so on.
- **Σ** means "add up one term for every value of *k*." The two forms on either side of the equals sign say exactly the same thing; the right-hand one is just shorter to write.
- The **∞** above the Σ is a convenience, not a claim that the game never ends. Every episode ends at a goal or at the clock, and every reward after that end is zero, so in practice the sum has a finite number of non-zero terms.

Each step further out is multiplied by another γ, so reward far away counts for less than reward now.

## Gamma, measured in Rocket League seconds

Gamma is usually described as "how much the bot cares about the future," which is true and useless. Here is a usable version. The discount weights γ⁰ + γ¹ + γ² + … add up to 1/(1 − γ). So the bot behaves roughly as if it were adding up about **1/(1 − γ) decisions** of undiscounted future. It is a rule of thumb, not a hard cutoff — there is no step at which the bot suddenly stops caring — but it is close enough to reason with.

RL Studio makes 15 decisions per second (120 Hz physics, `env.tick_skip = 8`). So divide by 15 to get seconds:

| `ppo.gamma` | ≈ decisions | ≈ seconds of play | What it feels like |
|---|---|---|---|
| 0.95 | 20 | 1.3 s | Very short-sighted. Good early: enough to connect "turn toward the ball" with "touch the ball." Cannot connect a kickoff to a goal. |
| 0.99 | 100 | 6.7 s | The usual default. Long enough to link a boost pickup, a drive and a shot. |
| 0.997 | 333 | 22 s | Long-sighted. Can in principle credit a defensive rotation for a goal fifteen seconds later, but the returns are much noisier and the critic has a harder job. |

Turn `ppo.gamma` **up** and the bot values distant consequences more, at the cost of noisier learning. Turn it **down** and learning gets steadier but the bot becomes greedy about the next second or two.

One trap worth naming: **gamma is measured in decisions, not seconds.** If you change [`env.tick_skip`](./actions.md), you change how many decisions fill a second, and the same gamma now means a different amount of Rocket League time. Halving tick skip to 4 gives 30 decisions per second, and `gamma = 0.99` drops from about 6.7 s to about 3.3 s of horizon. Change one, re-check the other.

## The value function: how good is this situation?

The **value function** answers one question: if the bot keeps playing the way it currently plays, starting from here, how much return should it expect?

```math
V^{\pi}(s) = \mathbb{E}_{\pi}\!\left[ G_t \mid s_t = s \right]
```

- **V^π(s)** is the value of observation *s* under policy π.
- **E_π[ · ]** is the expected value — the average over all the ways the rest of the episode could go, with the bot acting according to π.
- **G_t** is the return defined above, and **s_t = s** means "given that the observation at step *t* was *s*."

The superscript π matters. Value is always value *under a particular policy*. A state is not good or bad in the abstract; it is good or bad for the bot you currently have.

Concretely: the bot sitting between the ball and its own goal with 60 boost, facing the play, has a high V. The ball rolling toward its own open net with the bot on the far wall has a low V — possibly a large negative one, if conceding carries a negative reward. Same game, very different expected futures.

In RL Studio the value function is a second output head on the same network, trained alongside the policy ([The Training Loop](./training-loop.md) shows the shared trunk and the two heads). It is called the **critic**; the policy is the **actor**.

Nobody can compute that expectation — you would have to play out every possible future. The critic estimates it instead, and it learns the way any supervised model does. While the bot plays, RL Studio records the return that actually followed each observation, then nudges the critic's prediction for that observation toward the return that really happened. Do that a few million times and the output starts to approximate the average, because the one-off swings pull in both directions and cancel while the systematic part does not. It is a regression problem living inside a reinforcement learning problem. [PPO](./ppo.md) shows the exact loss term and the `ppo.value_coef` setting that weights it against the policy's own objective.

## From Q to advantage

There is a close cousin of V that conditions on the action too. The **action value** is:

```math
Q^{\pi}(s, a) = \mathbb{E}_{\pi}\!\left[ G_t \mid s_t = s,\ a_t = a \right]
```

Same symbols as before, plus **a**, the action taken at step *t*. In words: take action *a* right now, then go back to playing normally — what return should you expect? RL Studio does not train a Q network. Q is here only because it makes the next definition easy to state.

The **advantage** is the difference:

```math
A^{\pi}(s, a) = Q^{\pi}(s, a) - V^{\pi}(s)
```

- **A^π(s, a)** is the advantage of action *a* at observation *s*.
- **Q^π(s, a)** is the expected return if you take *a* here.
- **V^π(s)** is the expected return of doing whatever you normally do here.

In plain words: **how much better than average was this particular action, from this particular situation?**

A worked example. The bot arrives at a 50/50 challenge at the halfway line. Its habits say it boosts in about half the time and backs off about half the time. Boosting in comes out at advantage **+0.3**; backing off comes out at **−0.3**. (Those two numbers are invented for the example — [GAE](./gae.md) is what actually produces them.) They are equal and opposite for a reason: weighted by how often the bot picks each one, they have to balance out. That says nothing about how good the bot is. It says: from this exact spot, given this bot's mix of habits, committing was better than what this bot usually does, and retreating was worse. PPO will then make committing a bit more likely here and retreating a bit less likely.

Two things to hold onto. First, advantage is **relative to the bot's own current habits**, not to a perfect player. A bot that whiffs 90% of the time can still show a large positive advantage for a slightly-less-bad whiff. Second, because V is subtracted, advantage is centered on zero: weight each action by how often the bot picks it, add them up, and you get exactly zero, at every skill level. That falls straight out of the definition — averaging Q over the bot's own habits *is* V, so the average of Q − V is nothing.

That is a statement about the average, not about the count. It does not mean half the actions come out positive and half negative. One rare, much-better-than-usual action — the well-timed flick the bot almost never throws — can carry a large positive advantage while everything else sits slightly negative, and the weighted average is still zero. That skewed case is the interesting one, because it is the shape of most real improvement. What the zero-centering does guarantee is that a positive advantage is never evidence of a good bot. It is an above-personal-average decision, nothing more.

## Misconception: "the critic tells the bot what to do"

It does not. The critic never sees an action and never produces one. Given an observation it emits a single number — its guess at V(s) — and that number goes nowhere near the controller. In the deployed bot the critic is not even run.

The actor decides. The critic exists for exactly one reason: to make the actor's learning signal quieter. Here is why that is worth a whole second network.

You could score an action by the raw return that followed it. That is unbiased and correct on average. It is also extremely noisy, because the return includes *everything that happened afterwards* — the teammate who whiffed, the opponent who demoed you, the lucky bounce off the corner. Suppose the bot makes a genuinely good clear and the return that follows is −0.8 because a teammate own-goaled four seconds later. Score the clear by that return and you teach the bot the clear was bad.

Subtracting V(s) removes the part of that noise that was predictable in advance. V(s) already prices in the average badness of the situation the bot was in — including how often teammates in this bot's games whiff, and how much pressure its opponents usually apply. If a return of −0.8 is roughly what that situation normally produces, the advantage comes out near zero and the clear is scored as neither good nor bad. That is the honest answer.

Without the baseline, every action taken from a dangerous state would be scored as a mistake and every action taken from a safe state as a triumph, no matter what the bot pressed. A kickoff and a one-on-none open net produce wildly different returns, and that gap between situations is far larger than the gap between two candidate actions in the same situation. Subtracting V(s) cancels it. And because V(s) depends only on the situation, not on which action was taken, it strips that noise out without tilting the update in any direction. Same expected direction, far less variance, so fewer samples are needed per useful update. That is the actor-critic bargain, and it is why RL Studio trains two heads instead of one.

Now note what the baseline does **not** do. If the teammate's own goal was a one-off rather than typical, subtracting V(s) cannot remove it. Once the observation is fixed, V(s) is a single constant, so the whole −0.8 is still sitting in the return and still lands on the clear. Cutting the future short — so that a bad thing four seconds later stops being charged to this decision at all — is a separate mechanism with a separate knob. That one belongs to [GAE](./gae.md).

## Where the actual numbers come from

This page defines what an advantage **is**. It does not say how RL Studio computes one, because the true Q^π is never available — from any given situation the bot only ever plays out one future, not the average over all the futures that were possible. One stretch of collected play like that is called a **rollout**; [The Training Loop](./training-loop.md) shows how rollouts are gathered and stored. The estimator RL Studio actually uses, the knobs on it, and the reason a goal and a time-limit truncation must be handled differently all live in [GAE: Estimating the Advantage](./gae.md). What PPO does with the resulting number lives in [PPO: How the Policy Actually Changes](./ppo.md).

## What to look for in RL Studio

- **Value Loss panel.** This is how badly the critic's V(s) predictions missed the returns that actually happened. Read it as a rough sanity check only, alongside Explained Variance below. It is a squared error in raw reward units, so its size depends on how big the returns currently are — as the bot starts actually scoring, returns grow and value loss commonly *climbs* even though the critic is getting better. What matters is that it stays finite and does not run away. A steady climb while episode reward is flat is the shape worth investigating. It never needs to reach zero, because Rocket League is genuinely unpredictable and some error is irreducible.
- **Explained Variance panel.** The more honest critic check. It reads roughly 0 when the critic is no better than guessing the average return, and approaches 1 when it tracks the real returns closely. A healthy run starts near 0 — it can read slightly negative for the first few iterations, which is normal — climbs above roughly 0.5 early, and works toward 0.9 over the course of a run. Stuck near 0 for a long time means the advantages feeding PPO are mostly noise. [Reading the Training Graphs](./reading-the-graphs.md) covers what to change when that happens.
- **Policy inspector in the viewer.** Pause on a frame and look at the per-action probability bars: that is π(a | s) for the observation on screen, one bar per action. This is the most direct look you get at the policy. Watch how the bars concentrate as a run progresses — and note that the viewer usually plays the highest-probability action rather than sampling, which is why the bot in the viewer looks tidier than the bot that is training. [Exploration and Entropy](./exploration.md) explains that difference.
