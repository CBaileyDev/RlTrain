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

Training changes π. Nothing else about the bot changes. When people say "the bot got better", they mean the numbers inside π moved.

## Why the policy outputs probabilities, not a choice

A *deterministic* policy would map each observation to exactly one action: see this, press that. RL Studio's policy is *stochastic* instead. Its final layer produces one score per action in the discrete action table — 90 of them — and a softmax turns those scores into 90 probabilities that sum to 1. That is a **categorical distribution**. During training, RL Studio samples from it: an action with probability 0.30 is chosen about 30% of the time.

Two reasons this matters.

**It makes learning possible at all.** You cannot take a derivative of "the bot chose action 47." That is a discrete jump, and a gradient needs something that varies smoothly. But π(a | s) — *how often* action 47 gets chosen — is a smooth function of the network weights. Nudge a weight, the probability moves a little. That is the hook the whole learning algorithm hangs on, and [PPO](./ppo.md) shows how it is used.

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

In RL Studio the value function is a second output head, trained alongside the policy. It is called the **critic**; the policy is the **actor**.

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

A worked example. The bot arrives at a 50/50 challenge at the halfway line. Its habits say it boosts in about half the time and backs off about half the time. Boosting in comes out at advantage **+0.4**; backing off comes out at **−0.2**. That says nothing about how good the bot is. It says: from this exact spot, given this bot's mix of habits, committing was better than what this bot usually does, and retreating was worse. PPO will then make committing a bit more likely here and retreating a bit less likely.

Two things to hold onto. First, advantage is **relative to the bot's own current habits**, not to a perfect player. A bot that whiffs 90% of the time can still show a large positive advantage for a slightly-less-bad whiff. Second, because V is subtracted, advantage is centered near zero: roughly half of the actions the bot takes score positive and half negative, always, at every skill level. A positive advantage is not a good bot. It is an above-personal-average decision.

## Misconception: "the critic tells the bot what to do"

It does not. The critic never sees an action and never produces one. Given an observation it emits a single number — its guess at V(s) — and that number goes nowhere near the controller. In the deployed bot the critic is not even run.

The actor decides. The critic exists for exactly one reason: to make the actor's learning signal quieter. Here is why that is worth a whole second network.

You could score an action by the raw return that followed it. That is unbiased and correct on average. It is also extremely noisy, because the return includes *everything that happened afterwards* — the teammate who whiffed, the opponent who demoed you, the lucky bounce off the corner. Suppose the bot makes a genuinely good clear and the return that follows is −0.8 because a teammate own-goaled four seconds later. Score the clear by that return and you teach the bot the clear was bad.

Subtracting V(s) fixes this. V(s) already contains the average badness of the situation the bot was in — including the teammate's typical behavior and the opponent's typical pressure. What is left after subtraction is closer to the part that the action itself was responsible for. Same expected direction, far less noise, so fewer samples are needed per useful update. That is the actor-critic bargain, and it is why RL Studio trains two heads instead of one.

## Where the actual numbers come from

This page defines what an advantage **is**. It does not say how RL Studio computes one, because the true Q^π is never available — the bot only ever sees one rollout, not the average over all of them. The estimator RL Studio actually uses, the knobs on it, and the reason a goal and a time-limit truncation must be handled differently all live in [GAE: Estimating the Advantage](./gae.md). What PPO does with the resulting number lives in [PPO: How the Policy Actually Changes](./ppo.md).

## What to look for in RL Studio

- **Value Loss panel.** This is how badly the critic's V(s) predictions missed the returns that actually happened. Expect it to spike early and then settle. It does not need to reach zero — Rocket League is genuinely unpredictable, so some error is irreducible.
- **Explained Variance panel.** The more honest critic check. It reads roughly 0 when the critic is no better than guessing the average return, and approaches 1 when it tracks the real returns closely. A healthy run climbs from near 0 into the 0.5–0.9 range within the first several iterations. Stuck near 0 for a long time means the advantages feeding PPO are mostly noise. [Reading the Training Graphs](./reading-the-graphs.md) covers what to change when that happens.
- **Policy inspector in the viewer.** Pause on a frame and look at the per-action probability bars: that is π(a | s) for the observation on screen, one bar per action. This is the most direct look you get at the policy. Watch how the bars concentrate as a run progresses — and note that the viewer usually plays the highest-probability action rather than sampling, which is why the bot in the viewer looks tidier than the bot that is training. [Exploration and Entropy](./exploration.md) explains that difference.
