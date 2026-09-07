import type { Metric } from './types';

/** Keep the most recent observation at a step; never fabricate missing values. */
export function metricSeries(rows: Metric[], field: keyof Metric, smoothing = 0) {
  const points = new Map<number, number | null>();
  for (const row of rows) {
    if (Number.isFinite(row.steps) && row.steps >= 0)
      points.set(row.steps, Number.isFinite(row[field]) ? Number(row[field]) : null);
  }
  const sorted = [...points].sort((a, b) => a[0] - b[0]);
  const retention = Math.max(0, Math.min(0.99, smoothing));
  let previous: number | null = null;
  const smooth = sorted.map(([, value]) => {
    if (value === null) { previous = null; return null; }
    previous = previous === null ? value : retention * previous + (1 - retention) * value;
    return previous;
  });
  return { steps: sorted.map(([step]) => step), raw: sorted.map(([, value]) => value), smooth };
}

/** A relative performance estimate against ONE opponent, with half credit for draws.
 * One virtual win/loss regularizes sweeps. This is not ranked Rocket League MMR. */
export function relativeSkill(wins: number, losses: number, draws: number) {
  if (![wins, losses, draws].every(v => Number.isSafeInteger(v) && v >= 0)) return null;
  const matches = wins + losses + draws;
  if (!matches) return null;
  const score = wins + draws / 2;
  const p = (score + 1) / (matches + 2);
  return { matches, scoreRate: score / matches, eloDifference: Math.round(400 * Math.log10(p / (1 - p))) };
}
