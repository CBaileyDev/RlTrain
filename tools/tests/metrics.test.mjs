import { test } from 'node:test';
import assert from 'node:assert/strict';
import { metricSeries, relativeSkill } from '../../app/src/lib/metrics.ts';

test('chart sorts resumed counters, deduplicates and preserves invalid measurements as gaps', () => {
  const result = metricSeries([{steps:200,reward:4},{steps:100,reward:2},{steps:200,reward:6},{steps:300,reward:NaN},{steps:400,reward:10},{steps:NaN,reward:5}], 'reward', 0.5);
  assert.deepEqual(result.steps, [100,200,300,400]);
  assert.deepEqual(result.raw, [2,6,null,10]);
  assert.deepEqual(result.smooth, [2,4,null,10]);
});
test('smoothing off returns exact observed values, including negative loss', () => {
  assert.deepEqual(metricSeries([{steps:1,reward:-3},{steps:2,reward:2}], 'reward', 0).smooth, [-3,2]);
});
test('relative skill is finite for sweeps, symmetric, and refuses empty or invalid evidence', () => {
  assert.equal(relativeSkill(0,0,0), null);
  assert.equal(relativeSkill(-1,0,2), null);
  assert.equal(relativeSkill(1.5,0,0), null);
  assert.equal(relativeSkill(10,10,5).eloDifference, 0);
  assert.equal(relativeSkill(20,0,0).eloDifference, -relativeSkill(0,20,0).eloDifference);
  assert.equal(relativeSkill(0,0,20).scoreRate, 0.5);
});
