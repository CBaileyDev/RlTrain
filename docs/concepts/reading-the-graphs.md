# Reading the Training Graphs

**This page is a field guide to the RL Studio dashboard: for each number on screen, one sentence on what it is, what a healthy curve looks like, what the common broken shapes look like, and the one setting to change first.**

## Why this matters

Training a Rocket League bot is a long feedback loop. You press Train, and an hour later you have a
bot and a wall of graphs. The graphs are the only honest account of what happened. If you can read
them, you can kill a bad run in five minutes instead of losing a night to it.

This is a reference, not a tutorial. If the words *episode*, *policy*, *critic* or *advantage* are
new, read [what-is-rl.md](./what-is-rl.md) and [policy-and-value.md](./policy-and-value.md) first —
this page assumes them. Every entry below gives a one-line definition and then moves straight to
diagnosis. Nothing here teaches a concept from scratch — each entry links to the page that does. If
a definition here feels thin, that is deliberate: follow the link. Entropy is explained in
[exploration.md](./exploration.md), KL and clipping in [ppo.md](./ppo.md), explained variance and
advantages in [gae.md](./gae.md), reward terms in [rewards.md](./rewards.md), ELO in
[self-play.md](./self-play.md), and throughput in [training-loop.md](./training-loop.md).

## The 60-second triage

Open a run mid-flight and check these five things in order. Each one gets a full entry further down;
the one-clause gloss here is enough to run the check.

1. **Entropy — how spread out the bot's action choices still are — is above zero and falling
   slowly.** Falling is normal. Falling off a cliff is not, and neither is a flat line pinned at the
   top.
2. **Explained Variance — how much of the outcome the critic saw coming — is climbing** and is above
   roughly 0.4 after the first few hundred iterations. Below that, the critic is barely beating a
   constant guess and your advantages are mostly noise.
3. **Approximate KL — how far one update moved the policy — is in range**, somewhere around 0.005 to
   0.02 for a default config.
4. **Episode Reward Mean and Goals per Episode are moving together**, *once Ball Touch Rate has
   risen and levelled off*. Before touches rise, reward climbing while goals sit at zero is the
   normal early shape — the dense shaping terms are doing their job. After touches level off, reward
   up with goals flat is the warning sign.
5. **Steps per Second has not dropped** since the start of the run.

Five green checks means the run is healthy enough to leave alone. Any red check sends you to the
named failure mode near the end of this page.

## Numbers that are only meaningful in context

Reward magnitudes come entirely from your weight table. A run with `Goal` weighted 150 produces
completely different numbers than one with `Goal` weighted 10, and neither is "better". An Episode
Reward Mean of 12 means nothing on its own. Only its **trend inside one run** carries information.

The consequence is strict: **two runs with different reward configs cannot be compared by reward at
all.** To compare two configs, compare Goals per Episode, Ball Touch Rate, or ELO against a fixed
opponent. See [rewards.md](./rewards.md) and [self-play.md](./self-play.md).

## The metrics

Every entry uses the same four lines.

### Episode Reward Mean

**What it is.** The total reward one agent collected per episode, averaged over the episodes that
finished during this iteration.
**Healthy.** Noisy but trending up, with a steep early rise as the bot learns to touch the ball at
all, then a slower climb.
**Unhealthy.** Flat from the start (nothing is learning); climbing while Goals per Episode stays
flat (reward hacking); a sudden collapse (usually follows entropy collapse or a critic blow-up).
**First thing to change.** Nothing, on its own. This metric names the problem but never diagnoses
it. Read the per-term breakdown next.

### The per-term reward breakdown

**What it is.** The same reward, split into one line per term — `VelocityPlayerToBall`,
`StrongTouch`, `Goal`, `BoostPickup`, and the rest.
**Healthy.** Early on, the dense shaping terms dominate. Over the run, the event terms (touches,
goals) take a growing share while the shaping terms flatten.
**Unhealthy.** One term running away with the total while the others sit near zero. That term is
being farmed, not used.
**First thing to change.** Lower that term's weight in the reward table, or put it on a decaying
schedule. [rewards.md](./rewards.md) covers weight schedules.

### Episode Length

**What it is.** Average agent steps per episode before a goal or the time limit ends it. At the
default `env.tick_skip` of 8 against 120 Hz physics, the bot makes 15 decisions per second — so 15
agent steps is one second of Rocket League, and a 450-step episode is 30 seconds of play. Raise
`env.tick_skip` and every episode gets fewer steps without getting shorter in game time. See
[actions.md](./actions.md).
**Healthy.** Starts pinned at the time limit (nobody scores), then falls as the bot starts scoring.
**Unhealthy.** Pinned at the limit forever — the bot never scores. Collapsing to near zero — usually
a termination condition firing immediately, not skill.
**First thing to change.** If it never falls, check that the `Goal` weight is large enough to compete
with the dense shaping terms. Those pay out on every one of the thousands of steps in a long episode
while `Goal` pays once, and discounting shrinks a distant goal further in the value target the critic
is trained on. If it collapses, check your episode terminal conditions, starting with
`env.max_episode_seconds`.

### Goals per Episode

**What it is.** Goals scored per episode, counted for both teams in self-play.
**Healthy.** Near zero for a long time, then a clean upward break. This is the metric that
actually says the bot got better.
**Unhealthy.** Flat while Episode Reward Mean climbs. That is the reward-hacking signature.
**First thing to change.** The reward weights. Nothing else fixes a bot that is being paid for
something other than scoring.

### Ball Touch Rate

**What it is.** Ball touches per episode, per agent.
**Healthy.** The first metric to move at all. It should rise well before goals do.
**Unhealthy.** Still near zero after several hundred iterations — the bot has not found the ball.
Extremely high with no goals — the bot is dribbling or bumping the ball in place.
**First thing to change.** Raise the `VelocityPlayerToBall` weight to get first contact. If touches
are high but goals are not, raise `VelocityBallToGoal` instead.

### Entropy

**What it is.** How spread out the policy's probability is across the 90 discrete actions — high
entropy means it is still trying different things.
**Healthy.** Starts near the ceiling of **ln(90) ≈ 4.50 nats** (a uniform policy over 90 actions)
and drifts down slowly over the whole run, settling somewhere around 1.5 to 3.
**Unhealthy.** A plunge toward zero over a few dozen iterations. The bot has committed to one
behaviour and stopped exploring. See [exploration.md](./exploration.md).
**First thing to change.** Raise [`ppo.entropy_coef`](../settings-reference.md). Higher keeps the
policy spread out and exploring; lower lets it sharpen faster.

### Policy Loss

**What it is.** The value of PPO's clipped surrogate objective on the current batch.
**Healthy.** Hovering near zero, flipping sign, small in magnitude.
**Unhealthy.** There is almost no unhealthy shape here worth acting on.
**First thing to change.** Nothing. **Policy loss is not a progress metric.** In supervised
learning a falling loss means the model is getting better, because the dataset is fixed. PPO's
policy loss is computed against a surrogate objective whose data is regenerated from scratch every
iteration by the current policy. A "better" policy immediately gets a new, harder batch. So the
number hovers near zero regardless of how well training is going, and its sign and magnitude tell
you almost nothing. Judge progress by Episode Reward Mean, Entropy and Explained Variance instead.
[ppo.md](./ppo.md) explains what the surrogate actually is.

### Value Loss

**What it is.** How badly the value network's predictions miss the returns it was trained on.
**Healthy.** Rises early (the returns are getting bigger), then flattens or falls slowly. Absolute
size is meaningless — it scales with your reward weights.
**Unhealthy.** Climbing without limit, or spiking to enormous values.
**First thing to change.** Check reward scale first. Then lower
[`ppo.value_coef`](../settings-reference.md) — higher makes the critic dominate the shared trunk,
lower lets the policy keep control of it.

### Explained Variance

**What it is.** How much of the variance in actual returns the critic predicts, defined as
`1 - Var(returns - predicted) / Var(returns)`; 1.0 is perfect, 0.0 is no better than guessing the
mean, negative is worse than that.
**Healthy.** Climbs from near zero in the first iterations to 0.4–0.9 and stays there.
**Unhealthy.** Stuck near zero, or negative. The critic is not learning, so every advantage you
compute is noise. See [gae.md](./gae.md).
**First thing to change.** Check truncation handling — a time-limit ending must bootstrap from the
value estimate, a goal must not. Then raise `ppo.value_coef`.

### Approximate KL

**What it is.** An estimate of how far the updated policy moved away from the policy that collected
the data.
**Healthy.** Steady, roughly 0.005 to 0.02, no upward drift.
**Unhealthy.** Above about 0.05 and rising — each update is throwing the policy somewhere the
collected data cannot justify. Near zero — nothing is happening.
**First thing to change.** Lower [`ppo.learning_rate`](../settings-reference.md), then lower
[`ppo.epochs`](../settings-reference.md). Fewer epochs means each batch is reused less, so the
policy drifts less per iteration.

### Clip Fraction

**What it is.** The fraction of samples in the batch whose probability ratio hit the clip boundary.
**Healthy.** Roughly 0.05 to 0.25.
**Unhealthy.** Above 0.4 — most of the batch is clipped, so most of your gradient is being thrown
away. Near 0.0 — the updates are too timid to matter.
**First thing to change.** High: lower [`ppo.clip_range`](../settings-reference.md) or the learning
rate. Near zero: raise the learning rate or `ppo.epochs`.

### Advantage Mean and Standard Deviation (after normalization)

**What it is.** The centre and spread of the advantages after RL Studio normalizes them per
minibatch.
**Healthy.** Mean within about ±0.05 of zero, standard deviation close to 1.0, by construction.
**Unhealthy.** Standard deviation collapsing toward zero means every action in the batch looks
equally good and there is no learning signal left. NaN in either means the critic diverged.
**First thing to change.** This panel is a sanity check, not a knob. A collapsed spread points at
your reward — see [rewards.md](./rewards.md). NaN points at the critic.

### Gradient Norm

**What it is.** The size of the whole gradient vector before clipping.
**Healthy.** Steady, occasionally spiky, mostly below your clip threshold.
**Unhealthy.** Constantly pinned at the threshold (every update is being truncated) or spiking by
orders of magnitude.
**First thing to change.** Lower `ppo.learning_rate`. Adjust `ppo.max_grad_norm` only after that;
lowering it caps damage but hides the cause.

### Learning Rate

**What it is.** The current step size, which may be on a decay schedule.
**Healthy.** Exactly the schedule you configured.
**Unhealthy.** Decayed to near zero long before the run ends — the bot has effectively stopped
learning while still burning GPU time.
**First thing to change.** `ppo.learning_rate` and its schedule in
[settings-reference.md](../settings-reference.md).

### Steps per Second and Ticks per Second

**What it is.** Agent decisions per second, and physics ticks per second — related by your tick
skip, so 8 ticks per decision means ticks/sec is 8× steps/sec.
**Healthy.** Flat, and close to what `rl-engine bench` reported for this machine.
**Unhealthy.** Far below bench, or degrading over the run.
**First thing to change.** [`env.num_arenas`](../settings-reference.md) — see
[training-loop.md](./training-loop.md).

### Iteration Count

**What it is.** How many complete collect-then-update cycles have finished.
**Healthy.** Rising at a steady rate.
**Unhealthy.** Rising very slowly with healthy steps/sec means each iteration is enormous. That is
a choice, not a bug, but it makes every graph coarse.
**First thing to change.** Steps per iteration, in [settings-reference.md](../settings-reference.md).

### ELO

**What it is.** A skill rating from evaluation matches against fixed reference opponents.
**Healthy.** Rising, even when Episode Reward Mean is flat.
**Unhealthy.** Flat or falling while reward rises. Reward went up without skill going up.
**First thing to change.** Look at the reward breakdown, then the opponent pool settings in
[self-play.md](./self-play.md).

## Named failure modes

**1. Entropy collapse.** Entropy plunges toward zero, Episode Reward Mean flattens, and the viewer
shows the bot repeating one behaviour — the same boost-and-drive-past every kickoff. The policy
committed before it had explored. Raise `ppo.entropy_coef`, or lower `ppo.learning_rate` so the
policy sharpens more slowly. [exploration.md](./exploration.md).

**2. Reward hacking.** Episode Reward Mean climbs steadily while Goals per Episode stays flat. The
bot found a term that pays without scoring — for example farming `BoostPickup` in a loop around the
corner pads. Open the per-term breakdown, find the term carrying the total, and reweight it.
[rewards.md](./rewards.md).

**3. Critic divergence.** Value Loss climbs without bound and Explained Variance sits near zero or
goes negative. Advantages become noise, so the policy is being pushed in arbitrary directions.
Check your reward scale, check `ppo.value_coef`, and check the truncation-flag handling described in
[gae.md](./gae.md) — treating a time limit like a goal poisons every value target.

**4. Over-updating.** Approximate KL and Clip Fraction are both high. Each iteration is squeezing
too much out of one batch of data. Lower `ppo.epochs`, lower `ppo.learning_rate`, or lower
`ppo.clip_range`. [ppo.md](./ppo.md).

**5. Under-updating.** Clip Fraction is near zero, Approximate KL is near zero, and nothing on the
behaviour panels is moving. The updates are too small to matter. Raise `ppo.learning_rate` or
`ppo.epochs`.

**6. Data starvation.** Steps per Second is far below what `rl-engine bench` reported. The GPU is
waiting for simulation. Check `env.num_arenas` against your CPU thread count — too few arenas
starves the learner, too many oversubscribes the CPU and also slows down.
[training-loop.md](./training-loop.md).

**7. Self-play plateau.** Episode Reward Mean is flat but ELO against fixed opponents keeps rising.
**This is progress, not a stall.** In self-play the opponent improves at exactly the rate you do, so
reward can sit still forever while both sides get better. The mirror case — reward rising while ELO
is flat — is *not* progress. [self-play.md](./self-play.md).

## Symptom to page

| Symptom | Page that explains it |
|---|---|
| Entropy falling too fast or pinned at ln(90) | [exploration.md](./exploration.md) |
| Approximate KL or Clip Fraction out of range | [ppo.md](./ppo.md) |
| Explained Variance low, Value Loss climbing | [gae.md](./gae.md) |
| One reward term dominating the breakdown | [rewards.md](./rewards.md) |
| Reward up, goals flat | [rewards.md](./rewards.md) |
| Steps/sec low, iterations slow | [training-loop.md](./training-loop.md) |
| Reward flat, ELO rising (or the reverse) | [self-play.md](./self-play.md) |
| Not sure what an advantage sign means | [policy-and-value.md](./policy-and-value.md) |
| Bot behaves differently in the viewer than in training | [exploration.md](./exploration.md) |

## What to look for in RL Studio

This whole page is that section, so here is the dashboard itself.

The dashboard splits into three panel groups, top to bottom. **Behaviour** comes first: Episode
Reward Mean, the per-term reward breakdown, Episode Length, Goals per Episode, Ball Touch Rate and
ELO — what the bot is doing. **Learning** comes second: Entropy, Policy Loss, Value Loss, Explained
Variance, Approximate KL, Clip Fraction, advantage statistics, Gradient Norm and Learning Rate — how
the update is behaving. **Throughput** sits last: Steps per Second, Ticks per Second and Iteration
Count. The triage checklist reads one item from Learning, two from Behaviour and one from
Throughput, which is why it takes a minute rather than a scroll.

Click the pin icon on any panel to lift it into the pinned strip across the top of the dashboard.
Pinned panels stay visible while you scroll, which is how you watch Entropy and Goals per Episode
together without hunting for them.

To compare runs, open the run picker in the header and select a second run. Both series draw on
every panel, with the older run dimmed. Remember the rule above: if the two runs use different
reward configs, the reward panels are not comparable and only ELO, Goals per Episode and Ball Touch
Rate mean anything across them.

Every panel has an info icon in its top-right corner. Clicking it opens the concept page for that
metric in the in-app docs viewer, at the right section. That is the intended path out of this
guide — when a panel looks wrong and you want to understand why rather than just apply the fix,
click the icon.
