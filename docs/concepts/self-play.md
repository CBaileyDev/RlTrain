# Self-Play and Skill Rating

**Self-play means the bot's opponent is a frozen copy of the bot, which gives it an opponent that is always exactly as good as it is — and which also means the reward curve alone can never tell you whether the bot is actually getting better.**

## Why this matters

To learn to beat opponents, you need opponents. That sounds obvious until you try to find some. There is no library of Rocket League bots that ships one opponent for every skill level from "drives into the wall" to "flip-resets on demand." You could write a scripted opponent by hand, but a scripted opponent has a fixed set of habits, and once your bot finds the hole in those habits it wins every game forever. From that point on it learns nothing, because every episode produces the same outcome and the [advantage](./policy-and-value.md) of every action collapses toward zero.

Self-play solves the supply problem by making the bot fight itself. RL Studio loads the same policy weights into both dugouts. The blue car and the orange car are the same network, differing only in the mirrored observation described in [observations.md](./observations.md). The bot that cannot turn plays a bot that cannot turn. Ten million steps later, the bot that can air dribble plays a bot that can air dribble.

That is the whole trick, and it is worth naming: self-play is an **automatic curriculum**. The difficulty of the opposition tracks the skill of the learner for free, so the bot is permanently at the edge of what it can handle. Too-easy opponents teach nothing. Too-hard opponents produce nothing but losses and no gradient signal worth following. Self-play sits in the middle by construction.

Self-play is also why RL Studio's default reward config uses zero-sum terms — what one side gains the other loses. That mechanic belongs to [rewards.md](./rewards.md); read it there.

## The naive version, and how it fails

The simplest self-play is *latest versus latest*: every episode, both cars use the current weights. This works for a while and then quietly stops working.

Here is the Rocket League version of the failure. Early on, both bots over-commit — they boost straight at the ball on every kickoff and every 50/50. The policy discovers that sitting back and letting the opponent whiff is worth a lot of goals. Both sides learn that, because both sides are the same network. Now neither bot challenges. But once nobody challenges, the punishment for challenging disappears, and the policy that boosts straight at the ball starts winning again. So both sides start challenging. And now punishing challenges pays again.

The bot is not improving. It is walking in a circle in policy space, learning a counter to its own current habit, then a counter to that counter. This is **cycling**, and it is the standard failure mode of latest-versus-latest self-play. Nothing in the training signal notices, because against a mirror image the win rate hovers near 50% no matter which lap of the circle you are on.

## Opponent pools and policy versioning

The fix is to stop throwing away the past.

Every `selfplay.snapshot_interval` iterations, RL Studio takes the current policy weights, freezes a copy, and adds it to an **opponent pool**. The pool holds at most `selfplay.pool_size` past versions; when it is full, adding a new snapshot evicts an old one. When an episode starts, the opponent car is loaded with weights drawn from the pool rather than with the live weights.

What this buys you is a constraint. The bot no longer only has to beat its current self. It has to stay good against *everything it used to be*. A policy that abandons challenging entirely will get run over by the snapshot from 200 iterations ago that still challenges, so the gradient pushes back against the cycle. The pool also preserves old skills: if the bot at iteration 400 could do a decent ceiling shot and the bot at iteration 900 has forgotten how, iteration-400 keeps scoring ceiling shots until iteration-900 relearns the defense.

Note that pool opponents are frozen. They do not train. Only the live policy receives gradient updates; the snapshot is pure inference.

## Choosing who to play

`selfplay.opponent_sampling` controls how the opponent is drawn from the pool.

- **`uniform`** — every version in the pool is equally likely. This is the stable choice. Old, weak versions stay in the diet, which strongly damps cycling. The cost is wasted samples: once the bot beats the iteration-50 snapshot 99 times out of 100, episodes against it produce almost no useful advantage signal and mostly just burn wall-clock time.
- **`recency_weighted`** — recent snapshots are drawn more often, older ones rarely. Because recent snapshots are the strongest, nearly every episode is a hard game, and hard games carry the most learning signal per step. Progress is faster. The risk is that you have partly reinvented latest-versus-latest, so cycling risk comes back up.

The honest summary: `uniform` is slower and safer, `recency_weighted` is faster and twitchier. Start with `uniform` while you are still tuning rewards, and switch to `recency_weighted` once the run is stable and you want throughput.

## Non-stationarity: what self-play breaks

[what-is-rl.md](./what-is-rl.md) describes an environment with fixed rules — the same state and action always produce the same distribution over next states and rewards. Nearly all of RL's mathematics, [PPO](./ppo.md) included, assumes that.

In self-play, the opponent is part of the environment. And the opponent changes every time you snapshot. The assumption is violated, plainly and permanently. This is called **non-stationarity**, and three practical things follow:

1. **The value network is chasing a moving target.** The value network is trained to predict the return from a state. But the return from "ball at midfield, 40 boost, kickoff won" depends on how good the opponent is, and that keeps changing. Value targets computed 300 iterations ago describe a world that no longer exists.
2. **Explained variance is lower than in a fixed environment.** Do not read a self-play explained variance of 0.5 the way you would read 0.5 on a fixed task. See [reading-the-graphs.md](./reading-the-graphs.md) for the numbers to expect.
3. **A flat reward curve is ambiguous.** It can mean "the bot stopped improving." It can equally mean "the bot is improving exactly as fast as its opponent." These two situations look identical on the reward graph and require opposite responses from you.

## The misconception you must not fall for

> "Episode reward is going up, so the bot is getting better."

In self-play this does not follow. Episode reward is measured against an opponent that is also changing. Reward can rise because your bot improved. Reward can *fall* while your bot improved, because the opponent improved faster. Reward can sit perfectly flat through the most productive hour of the whole run.

Only a match against a **fixed reference opponent** tells you anything about absolute skill. That is the entire reason RL Studio keeps a set of benchmark opponents that never train, and the reason the ELO panel exists.

## ELO

ELO turns match results into a single number per policy version. It has two halves.

**Expected score.** Before the match, ELO predicts what fraction of the points player A should take:

```math
E_A = \frac{1}{1 + 10^{(R_B - R_A)/400}}
```

- $E_A$ — the expected fraction of points A takes, between 0 and 1. For a single game with no draws, read it as A's win probability.
- $R_A$ — player A's current rating.
- $R_B$ — player B's current rating.
- $400$ — the scale constant. It sets what a rating gap *means*: a 400-point gap makes the stronger player's expectation ten times the weaker player's, so $E_A = 1/11$ when B is 400 points ahead.

**The update.** After the match:

```math
R_A^{\text{new}} = R_A + K\,(S_A - E_A)
```

- $S_A$ — the actual result: 1 for a win, 0.5 for a draw, 0 for a loss.
- $E_A$ — the expectation from the formula above.
- $K$ — the step size, the maximum number of rating points one game can move you.

A large $K$ (say 64) makes the ELO curve responsive and jumpy — a new snapshot finds its true level in a handful of matches, but the line is noisy. A small $K$ (say 8) makes a smooth line that lags reality by dozens of matches. It is a learning rate, with the same trade-off.

**Worked example.** Snapshot A is rated 1500, snapshot B is rated 1400, and $K = 32$.

$R_B - R_A = -100$, so $10^{-100/400} = 10^{-0.25} \approx 0.562$, and $E_A = 1/1.562 \approx 0.640$. A is expected to take 64% of the points.

If A wins: $R_A^{\text{new}} = 1500 + 32(1 - 0.640) = 1511.5$. A gains 11.5 points for a win it was supposed to get.
If A loses: $R_A^{\text{new}} = 1500 + 32(0 - 0.640) = 1479.5$. A drops 20.5 points for an upset. B moves by the mirror amount.

## Reading ELO honestly

Two warnings, both easy to trip over.

**Ratings inside one ladder are internally comparable but can inflate as a group.** ELO only measures relative strength among the players who actually played each other. A self-play ladder is a closed room. If every snapshot in the pool has the same blind spot, they will all rate highly against each other while losing to anything from outside.

**A rating from run A is not comparable to a rating from run B.** Different runs are different closed rooms. Comparing 1620 from Monday's run against 1580 from Tuesday's run is meaningless unless both played the same fixed benchmark opponents.

When you want a real answer, run a head-to-head instead of trusting the ladder:

```
rl-engine eval --a runs/run-2026-09-02/ckpt-1200.pt --b runs/run-2026-09-05/ckpt-0800.pt --matches 20
```

That plays 20 fixed matches between two specific checkpoints and reports the score. "Checkpoint 1200 beat checkpoint 800 fourteen to six" is a fact. "Checkpoint 1200 has an ELO of 1620" is a fact about one ladder.

## Settings

- **`selfplay.enabled`** — off means both cars use the live policy, latest-versus-latest, no pool. Useful only for debugging.
- **`selfplay.snapshot_interval`** — iterations between frozen copies. Too frequent and the pool fills with near-identical policies, which is functionally the same as latest-versus-latest and gives back the cycling protection you paid for. Too rare and the pool's newest member is far behind the live policy, so most episodes are blowouts that teach little.
- **`selfplay.pool_size`** — how many versions are retained. Cost is one full set of weights per entry, held for inference. A larger pool covers more of the bot's history and damps cycling harder; it also spends more of your samples on opponents you already dominate.
- **`selfplay.opponent_sampling`** — `uniform` or `recency_weighted`, as above.

The snapshot itself happens at a fixed point in the iteration; see [training-loop.md](./training-loop.md) for where it lands.

## What to look for in RL Studio

- **The ELO curve.** This, not episode reward, is the "is my bot improving" graph. Rising is good. Flat while episode reward rises means the bot and its opponents are improving together and you have learned nothing about absolute skill. Rising while reward is flat is the pleasant case people miss.
- **The opponent-pool panel.** It lists every version currently in the pool, its snapshot iteration, and the live policy's win rate against it. Healthy looks like a gradient: near 100% against the oldest entries, sliding toward roughly 50% against the newest. If the win rate against a 500-iteration-old snapshot is sitting near 50%, you have made no absolute progress in 500 iterations — most likely cycling. If it is near 50% against *everything including the oldest entry*, check your rewards before blaming self-play.
- **The head-to-head matrix from `rl-engine eval` runs.** A grid of checkpoints with the score of each pairing. This is the ground truth the ELO curve is an approximation of. When ELO and the matrix disagree, believe the matrix.
