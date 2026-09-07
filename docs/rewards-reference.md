# Rewards reference

Dense rewards are rates multiplied by elapsed simulated seconds. Events pay once per
decision. Reward measures the recipe, not playing strength.

| Term | Default | Calculation |
| --- | --- | --- |
| `velocityToBall` | 4 | car velocity projected toward ball / 2300 × seconds |
| `faceBall` | 0.25 | forward direction dot direction to ball × seconds |
| `ballToGoal` | 2 | signed ball Y velocity / 6000 × seconds |
| `saveBoost` | 0.2 | boost / 100 × seconds |
| `airTime` | 0 | airborne flag × seconds |
| `touch` | 5 | one payment if a valid touch occurred during the decision |
| `boostPickup` | 10 | positive net boost increase / 100 |
| `goal` | 150 | +weight for scoring team, negative weight for conceding team |

## Interpretation and limitations

Boost pickup measures net boost increase across a decision. Simultaneous consumption
can undercount a pickup. Multiple touches inside one decision pay once. Goal callbacks
are latched for the decision, then the arena resets.

Live changes apply at decision boundaries and are acknowledged by the engine. Changing
weights changes reward scale. Compare models with fixed-settings evaluations.

There are no bump/demo, save, flip-reset or zero-sum wrapper rewards in this release.
The concept handbook discusses broader designs as learning material; the table above
is the implemented editable catalog.
