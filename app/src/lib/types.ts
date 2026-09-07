export type Config = Record<string, string | number | boolean>;
export interface Metric {
  iteration: number;
  steps: number;
  reward: number;
  entropy: number;
  valueLoss: number;
  policyLoss: number;
  kl: number;
  clipFraction: number;
  explainedVariance: number;
  stepsPerSecond: number;
  touches: number;
  goals: number;
  episodes: number;
  elapsedSeconds: number;
}
export interface Car {
  id: number;
  team: number;
  pos: [number, number, number];
  forward: [number, number, number];
  up: [number, number, number];
  boost: number;
  demoed: boolean;
}
export interface Frame {
  tick: number;
  ball: [number, number, number];
  cars: Car[];
  score: [number, number];
}
export interface Checkpoint {
  path: string;
  metadata: { iteration: number; steps: number };
}
export interface Run {
  id: string;
  path: string;
  config: Config;
  summary: { status: string; steps?: number };
  checkpoints: Checkpoint[];
}
export interface Rule {
  type: string;
  default: string | number | boolean;
  description: string;
  minimum?: number;
  maximum?: number;
  enum?: string[];
}
export interface Proposal {
  explanation: string;
  changes: Config;
}
export const rewardKeys = [
  'velocityToBall',
  'faceBall',
  'ballToGoal',
  'saveBoost',
  'airTime',
  'touch',
  'boostPickup',
  'goal',
];
export const native = '__TAURI_INTERNALS__' in window;
export async function invoke<T>(command: string, args?: Record<string, unknown>): Promise<T> {
  if (!native)
    throw new Error('Open the desktop app to run the engine. This browser view is a UI preview.');
  const api = await import('@tauri-apps/api/core');
  return api.invoke<T>(command, args);
}
export function errorMessage(error: unknown): string {
  if (typeof error === 'object' && error && 'message' in error) return String(error.message);
  return String(error);
}
export function label(key: string): string {
  return key.replace(/([A-Z])/g, ' $1').replace(/^./, (c) => c.toUpperCase());
}
