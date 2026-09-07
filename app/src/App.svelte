<script lang="ts">
  import { onMount } from 'svelte';
  import Chart from './lib/Chart.svelte';
  import MatchView from './lib/MatchView.svelte';
  import Document from './lib/Document.svelte';
  import schemaData from '../../configs/schema/run.schema.json';
  import teamPreset from '../../configs/presets/3v3-team.json';
  import { invoke, native, errorMessage, label, rewardKeys } from './lib/types';
  import type { Config, Metric, Frame, Run, Rule, Proposal } from './lib/types';

  const schema = schemaData.properties as Record<string, Rule>;
  const defaults: Config = Object.fromEntries(
    Object.entries(schema).map(([k, v]) => [k, v.default]),
  );
  const documents = import.meta.glob('../../docs/**/*.md', {
    query: '?raw',
    import: 'default',
    eager: true,
  }) as Record<string, string>;
  const pages = ['Overview', 'Train', 'Rewards', 'Match', 'Runs', 'Assistant', 'Learn', 'Settings'];
  const pageDescriptions: Record<string, string> = {
    Overview: 'A clear view of what your agent is learning.',
    Train: 'Shape an experiment. Let the policy find its own way.',
    Rewards: 'Tell your agent what matters, one behavior at a time.',
    Match: 'Watch decisions turn into movement.',
    Runs: 'Every experiment leaves a trail. Compare, resume, and replay.',
    Assistant: 'Turn training measurements into a better next experiment.',
    Learn: 'Understand the ideas behind every update.',
    Settings: 'Make this workspace your own.',
  };
  const settingGroups = {
    Environment: [
      'name',
      'arena',
      'meshPath',
      'teamSize',
      'observation',
      'seed',
      'episodeSeconds',
      'noTouchSeconds',
    ],
    Throughput: [
      'device',
      'arenas',
      'threads',
      'tickSkip',
      'actionDelayTicks',
      'rolloutSteps',
      'minibatchSize',
    ],
    Learning: [
      'iterations',
      'hiddenSize',
      'epochs',
      'learningRate',
      'gamma',
      'gaeLambda',
      'clipRange',
      'entropyCoef',
      'valueCoef',
      'maxGradNorm',
      'checkpointEvery',
    ],
  };
  let page = $state('Overview');
  let content: HTMLElement;
  let config = $state<Config>({ ...defaults });
  let preferencesReady = $state(false);
  let rows = $state<Metric[]>([]);
  let frame = $state<Frame | null>(null);
  let status = $state('idle');
  let busy = $state(false);
  let available = $state(false);
  let error = $state('');
  let notice = $state('');
  let runPath = $state('');
  let runConfig = $state<Config | null>(null);
  let device = $state('');
  let runs = $state<Run[]>([]);
  let checkpoint = $state('');
  let opponent = $state('');
  let matchCount = $state(20);
  let theme = $state(localStorage.getItem('rlstudio-theme') ?? 'forest');
  let onboarding = $state(localStorage.getItem('rlstudio-onboarded') !== 'yes');
  let log = $state<string[]>([]);
  let question = $state('What do these metrics suggest I should try next?');
  let proposal = $state<Proposal | null>(null);
  let asking = $state(false);
  let apiKey = $state('');
  let model = $state(localStorage.getItem('rlstudio-model') ?? '');
  let docKey = $state('../../docs/concepts/what-is-rl.md');
  let docSearch = $state('');
  let comparePath = $state('');
  let comparison = $state<Metric[]>([]);
  let evaluation = $state('');
  let benchmark = $state('');
  const latest = $derived(rows.at(-1));
  const shownConfig = $derived(runConfig ?? config);
  const active = $derived(
    busy || ['starting', 'running', 'paused', 'playing', 'stopping'].includes(status),
  );
  const checkpoints = $derived(
    runs.flatMap((run) =>
      run.checkpoints.map((cp) => ({
        ...cp,
        name: `${run.config.name} · iteration ${cp.metadata.iteration}`,
      })),
    ),
  );
  const filteredDocs = $derived(
    Object.keys(documents).filter(
      (k) =>
        !k.includes('/internal/') &&
        (k + documents[k]).toLowerCase().includes(docSearch.toLowerCase()),
    ),
  );
  $effect(() => {
    document.documentElement.dataset.theme = theme;
    localStorage.setItem('rlstudio-theme', theme);
  });
  $effect(() => {
    void page;
    content?.scrollTo({ top: 0 });
  });
  $effect(() => {
    localStorage.setItem('rlstudio-model', model);
  });
  $effect(() => {
    if (preferencesReady && !validate(config))
      localStorage.setItem('rlstudio-config', JSON.stringify(config));
  });
  function number(value: number | undefined, digits = 0) {
    return value === undefined
      ? '—'
      : value.toLocaleString(undefined, { maximumFractionDigits: digits });
  }
  function samplesPerUpdate(candidate: Config = config) {
    return (
      Number(candidate.arenas) *
      Number(candidate.teamSize) *
      2 *
      Number(candidate.rolloutSteps)
    );
  }
  function setTrainingStepTarget(event: Event) {
    const requested = (event.currentTarget as HTMLInputElement).valueAsNumber;
    if (!Number.isFinite(requested) || requested < 1) return;
    const updates = Math.ceil(requested / samplesPerUpdate());
    config.iterations = Math.min(1_000_000, Math.max(1, updates));
  }
  async function safely(action: () => Promise<unknown>) {
    error = '';
    try {
      await action();
    } catch (cause) {
      error = errorMessage(cause);
    }
  }
  async function refreshRuns() {
    if (native) runs = await invoke<Run[]>('list_runs');
  }
  async function windowAction(action: 'minimize' | 'toggleMaximize' | 'close') {
    await safely(async () => {
      if (native) {
        const { getCurrentWindow } = await import('@tauri-apps/api/window');
        await getCurrentWindow()[action]();
      }
    });
  }
  function validate(candidate: Config = config): string {
    for (const [key, rule] of Object.entries(schema)) {
      const value = candidate[key];
      if (rule.enum && !rule.enum.includes(String(value)))
        return `Unsupported value for ${label(key)}.`;
      if (rule.type === 'string' && typeof value !== 'string') return `${label(key)} must be text.`;
      if (rule.type === 'number' || rule.type === 'integer') {
        if (
          typeof value !== 'number' ||
          !Number.isFinite(value) ||
          (rule.type === 'integer' && !Number.isInteger(value)) ||
          (rule.minimum !== undefined && value < rule.minimum) ||
          (rule.maximum !== undefined && value > rule.maximum)
        )
          return `${label(key)} is outside its allowed range.`;
      }
    }
    if (Number(candidate.actionDelayTicks) >= Number(candidate.tickSkip))
      return 'Action delay must be smaller than tick skip.';
    if (
      Number(candidate.minibatchSize) >
      Number(candidate.arenas) * Number(candidate.teamSize) * 2 * Number(candidate.rolloutSteps)
    )
      return 'Minibatch size exceeds the rollout batch.';
    return '';
  }
  async function start(mode: 'train' | 'play' | 'eval' | 'bench', resume = false) {
    await safely(async () => {
      const problem = validate();
      if (problem) throw new Error(problem);
      busy = true;
      try {
        rows = [];
        frame = null;
        evaluation = '';
        benchmark = '';
        runPath = '';
        status = 'starting';
        await invoke('start_engine', {
          mode,
          config: $state.snapshot(config),
          checkpoint: mode === 'train' && !resume ? null : checkpoint || null,
          opponent: opponent || null,
          matches: matchCount,
        });
        if (mode === 'train') page = 'Overview';
        if (mode === 'play' || mode === 'eval') page = 'Match';
      } catch (e) {
        status = 'failed';
        throw e;
      } finally {
        busy = false;
      }
    });
  }
  async function control(type: string) {
    await safely(async () => {
      await invoke('engine_control', { command: { type } });
      if (type === 'stop') status = 'stopping';
    });
  }
  async function applyRewards() {
    await safely(async () => {
      const problem = validate();
      if (problem) throw new Error(problem);
      if (active)
        await invoke('engine_control', {
          command: {
            type: 'rewards',
            values: Object.fromEntries(rewardKeys.map((k) => [k, config[k]])),
          },
        });
      notice = active
        ? 'Reward update sent. The engine will acknowledge it at a decision boundary.'
        : 'Reward weights are ready for your next run.';
    });
  }
  async function inspect(run: Run) {
    await safely(async () => {
      rows = await invoke<Metric[]>('run_metrics', { path: run.path });
      runPath = run.path;
      runConfig = run.config;
      page = 'Overview';
    });
  }
  async function compare() {
    await safely(async () => {
      comparison = comparePath ? await invoke<Metric[]>('run_metrics', { path: comparePath }) : [];
    });
  }
  function exportConfig() {
    const link = document.createElement('a');
    link.href = URL.createObjectURL(
      new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' }),
    );
    link.download = 'rlstudio-config.json';
    link.click();
    URL.revokeObjectURL(link.href);
  }
  async function importConfig(event: Event) {
    await safely(async () => {
      const file = (event.target as HTMLInputElement).files?.[0];
      if (!file) return;
      const parsed: unknown = JSON.parse(await file.text());
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed))
        throw new Error('Expected a configuration object.');
      for (const [key, value] of Object.entries(parsed)) {
        if (!schema[key] || !['string', 'number', 'boolean'].includes(typeof value))
          throw new Error(`Invalid setting: ${key}`);
      }
      const candidate = { ...defaults, ...(parsed as Config) };
      const problem = validate(candidate);
      if (problem) throw new Error(problem);
      config = candidate;
      notice = 'Configuration imported.';
    });
  }
  async function openDoc(href: string) {
    await safely(async () => {
      if (/^https?:/.test(href)) {
        if (native) {
          const { openUrl } = await import('@tauri-apps/plugin-opener');
          await openUrl(href);
        } else window.open(href, '_blank', 'noopener,noreferrer');
      } else {
        const key =
          '../../' +
          new URL(href, 'https://docs.invalid/' + docKey.replace('../../', '')).pathname.slice(1);
        if (documents[key] && !key.includes('/internal/')) docKey = key;
        else notice = 'This linked file is available in the project repository: ' + href;
      }
    });
  }
  function focusDialog(node: HTMLElement) {
    const previous = document.activeElement as HTMLElement | null;
    node.focus();
    const keydown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        onboarding = false;
        return;
      }
      if (event.key !== 'Tab') return;
      const elements = Array.from(
        node.querySelectorAll<HTMLElement>('button, input, select, textarea, [tabindex="0"]'),
      );
      const first = elements[0],
        last = elements.at(-1);
      if (event.shiftKey && (document.activeElement === first || document.activeElement === node)) {
        event.preventDefault();
        last?.focus();
      } else if (!event.shiftKey && document.activeElement === last) {
        event.preventDefault();
        first?.focus();
      }
    };
    node.addEventListener('keydown', keydown);
    return {
      destroy() {
        node.removeEventListener('keydown', keydown);
        previous?.focus();
      },
    };
  }
  async function ask() {
    asking = true;
    proposal = null;
    await safely(async () => {
      proposal = await invoke<Proposal>('ask_assistant', {
        question,
        config: $state.snapshot(config),
        metrics: $state.snapshot(rows.slice(-30)),
        model,
      });
    });
    asking = false;
  }
  function localReview() {
    if (!latest) {
      proposal = {
        explanation:
          'Collect several PPO updates before making a change. Start with the baseline and observe touches, entropy, and evaluation outcomes. This is a local rule-based check, not an AI response.',
        changes: {},
      };
      return;
    }
    const changes: Config = {};
    const findings: string[] = [
      'Local rule-based check. Reward alone does not measure playing strength.',
    ];
    if (latest.kl > 0.03) {
      findings.push('Policy movement is large. Try halving the learning rate for the next run.');
      changes.learningRate = Math.max(0.0000001, Number(config.learningRate) / 2);
    }
    if (latest.entropy < 1) {
      findings.push(
        'The policy has narrowed its action choices. A small entropy increase may restore exploration.',
      );
      changes.entropyCoef = Math.min(1, Number(config.entropyCoef) + 0.01);
    }
    if (rows.slice(-5).every((r) => r.touches === 0)) {
      findings.push(
        'Recent rollouts have no ball touches. Increase the movement-to-ball signal modestly, then evaluate again.',
      );
      changes.velocityToBall = Math.min(200, Number(config.velocityToBall) + 1);
    }
    if (findings.length === 1)
      findings.push(
        'No simple instability trigger fired. Keep the baseline and compare saved models over more matches.',
      );
    proposal = { explanation: findings.join('\n\n'), changes };
  }
  async function applyProposal() {
    if (!proposal) return;
    const proposed = proposal;
    config = { ...config, ...proposed.changes };
    if (active && Object.keys(proposed.changes).every((k) => rewardKeys.includes(k)))
      await applyRewards();
    else notice = 'Proposal copied into the next-run configuration. Review it in Train.';
  }
  onMount(() => {
    try {
      const saved = JSON.parse(localStorage.getItem('rlstudio-config') ?? 'null') as Config | null;
      if (saved) {
        const candidate = { ...defaults, ...saved };
        if (!validate(candidate)) config = candidate;
      }
    } catch {
      /* An older or damaged preference never blocks startup. */
    }
    preferencesReady = true;
    let disposed = false;
    const cleanup: (() => void)[] = [];
    void safely(async () => {
      if (!native) return;
      const { listen } = await import('@tauri-apps/api/event');
      const stop = await listen<Record<string, unknown>>('engine-event', (event) => {
        const data = event.payload;
        switch (data.type) {
          case 'started':
            status = 'running';
            runPath = String(data.run);
            device = String(data.device);
            runConfig = data.config as Config;
            break;
          case 'metrics':
            rows = [...rows.slice(-1999), data as unknown as Metric];
            break;
          case 'frame':
            frame = data as unknown as Frame;
            break;
          case 'status':
            status = String(data.status);
            break;
          case 'config':
            runConfig = data.config as Config;
            notice = 'Engine acknowledged the new reward weights.';
            break;
          case 'error':
            error = String(data.message);
            break;
          case 'checkpoint':
            void safely(refreshRuns);
            break;
          case 'evaluation':
            evaluation = `${data.matches} matches · Blue ${data.blueWins} wins · Orange ${data.orangeWins} wins · ${data.draws} draws`;
            break;
          case 'benchmark':
            benchmark = `${number(Number(data.physicsTicksPerSecond))} physics ticks/s · ${number(Number(data.agentStepsPerSecond))} agent steps/s (one arena)`;
            break;
          case 'exit':
            status =
              Number(data.code) === 0
                ? status === 'stopped' || status === 'stopping'
                  ? 'stopped'
                  : 'completed'
                : 'failed';
            if (status === 'failed' && !error)
              error = `Engine exited with code ${data.code}. Check Settings → Engine log.`;
            void refreshRuns().catch((cause) => {
              error = errorMessage(cause);
            });
            break;
        }
      });
      if (disposed) stop();
      else cleanup.push(stop);
      const stopLog = await listen<string>('engine-log', (event) => {
        log = [...log.slice(-199), event.payload];
      });
      if (disposed) stopLog();
      else cleanup.push(stopLog);
      const env = await invoke<{ engineAvailable: boolean }>('environment');
      available = env.engineAvailable;
      await refreshRuns();
    });
    return () => {
      disposed = true;
      cleanup.forEach((fn) => fn());
    };
  });
</script>

<div class="shell" inert={onboarding}>
  <header class="titlebar">
    <div data-tauri-drag-region class="drag-region">
      <span class="brand-mark">RL</span><strong>STUDIO</strong><span class="title-divider"
      ></span><span>Reinforcement learning workbench</span>
    </div>
    <div class="window-controls">
      <button aria-label="Minimize window" onclick={() => windowAction('minimize')}>−</button
      ><button aria-label="Maximize window" onclick={() => windowAction('toggleMaximize')}>□</button
      ><button aria-label="Close window" onclick={() => windowAction('close')}>×</button>
    </div>
  </header>
  <aside class="sidebar">
    <div class="workspace-label">WORKSPACE <span>LOCAL</span></div>
    <nav aria-label="Main navigation">
      {#each pages as item, i}<button class:selected={page === item} onclick={() => (page = item)}
          ><span class="nav-index">{String(i + 1).padStart(2, '0')}</span
          >{item}{#if item === 'Train' && active}<span class="live-dot"></span>{/if}</button
        >{/each}
    </nav>
    <div class="sidebar-bottom">
      <div class="engine-state">
        <span class:connected={available} class="state-light"></span>{native
          ? available
            ? 'Engine ready'
            : 'Engine missing'
          : 'Browser preview'}
      </div>
      <p>Built for curiosity.<br />Measured in experiments.</p>
      <button class="text-button" onclick={() => (onboarding = true)}>Getting started ↗</button>
    </div>
  </aside>
  <main bind:this={content}>
    <div class="page-heading">
      <div>
        <div class="eyebrow">YOUR LEARNING LAB</div>
        <h1>{page}</h1>
        <p>{pageDescriptions[page]}</p>
      </div>
      <div class="heading-actions">
        <span class="status-badge" class:running={active}
          >{status}{device ? ` · ${device}` : ''}</span
        >{#if active}<button onclick={() => control(status === 'paused' ? 'resume' : 'pause')}
            >{status === 'paused' ? 'Resume' : 'Pause'}</button
          ><button class="danger" onclick={() => control('stop')} disabled={status === 'stopping'}
            >Stop & save</button
          >{:else}<button
            class="primary"
            disabled={!available || busy}
            onclick={() => start('train')}>Start training <span>↗</span></button
          >{/if}
      </div>
    </div>
    {#if error}<div class="alert error" role="alert">
        <strong>Something needs attention</strong>
        <p>{error}</p>
        <button aria-label="Dismiss error" onclick={() => (error = '')}>×</button>
      </div>{/if}
    {#if notice}<div class="alert notice" role="status">
        {notice}<button aria-label="Dismiss notification" onclick={() => (notice = '')}>×</button>
      </div>{/if}
    {#if !native}<div class="alert notice">
        Interface preview. Open RL Studio on the desktop to train and manage local models.
      </div>{/if}

    {#if page === 'Overview'}
      <div class="metric-strip">
        {#each [['Agent steps', number(latest?.steps), 'Experience collected'], ['Mean reward', number(latest?.reward, 4), 'Per agent decision'], ['Throughput', number(latest?.stepsPerSecond), 'Agent steps / second'], ['Policy entropy', number(latest?.entropy, 3), 'Action diversity · max 4.50']] as metric}<div
          >
            <span>{metric[0]}</span><strong>{metric[1]}</strong><small>{metric[2]}</small>
          </div>{/each}
      </div>
      <div class="overview-grid">
        <MatchView {frame} />
        <section class="experiment-panel">
          <div class="panel-heading">
            <h3>Current experiment</h3>
            <span>PPO</span>
          </div>
          <h2>{String(shownConfig.name)}</h2>
          <p class="muted">
            {shownConfig.teamSize}v{shownConfig.teamSize} self-play · {shownConfig.arena ===
            'practice'
              ? 'Practice Arena'
              : 'Accurate Arena'}
          </p>
          <dl>
            <div>
              <dt>Parallel arenas</dt>
              <dd>{shownConfig.arenas}</dd>
            </div>
            <div>
              <dt>Iteration</dt>
              <dd>{latest?.iteration ?? '—'}</dd>
            </div>
            <div>
              <dt>Ball touches</dt>
              <dd>{latest?.touches ?? '—'}</dd>
            </div>
            <div>
              <dt>Goals this update</dt>
              <dd>{latest?.goals ?? '—'}</dd>
            </div>
          </dl>
          <p class="help-note">
            Both teams share a policy. Each update uses real simulator experience, then adjusts the
            network with PPO.
          </p>
          <button onclick={() => (page = 'Train')}>Inspect configuration →</button>
        </section>
      </div>
      <div class="charts-grid">
        <Chart {rows} field="reward" title="Reward per decision" /><Chart
          {rows}
          field="entropy"
          title="Policy entropy"
          color="#87afea"
        /><Chart {rows} field="valueLoss" title="Value loss" color="#d8ac79" /><Chart
          {rows}
          field="kl"
          title="Approximate KL divergence"
          color="#9db7bb"
        />
      </div>
      {#if runPath}<p class="path" data-selectable>{runPath}</p>{/if}
      {#if comparison.length}<h2 class="section-title">Comparison run</h2>
        <div class="charts-grid">
          <Chart rows={comparison} field="reward" title="Comparison reward" color="#d8ac79" /><Chart
            rows={comparison}
            field="entropy"
            title="Comparison entropy"
            color="#d8ac79"
          />
        </div>{/if}
    {:else if page === 'Train'}
      <div class="toolbar">
        <button onclick={exportConfig}>Export config</button><label class="file-button"
          >Import config<input type="file" accept=".json" onchange={importConfig} /></label
        ><button onclick={() => {
          config = { ...defaults, ...teamPreset };
          notice = '3v3 baseline loaded. Start a new model: 1v1 checkpoints are incompatible. Target is additional steps for this session, not a skill guarantee.';
        }}>Load 3v3 baseline</button
        ><button
          onclick={() => {
            config = { ...defaults };
            notice = 'Baseline restored.';
          }}>Reset to baseline</button
        ><label class="field training-target" for="training-step-target"
          ><span>Stop after agent steps</span><input
            id="training-step-target"
            type="number"
            min={samplesPerUpdate()}
            step={samplesPerUpdate()}
            value={samplesPerUpdate() * Number(config.iterations)}
            oninput={setTrainingStepTarget}
          /><small
            >Rounds up to a full PPO update. Current plan: {number(samplesPerUpdate() * Number(config.iterations))} agent steps over {number(Number(config.iterations))} updates.</small
          ></label
        ><span>Each update · {number(samplesPerUpdate())} agent steps</span>
      </div>
      {#if active}<p class="help-note">
          Edits here configure the next run. Use Rewards to change the active run.
        </p>{/if}
      {#each Object.entries(settingGroups) as [group, keys]}<section class="settings-section">
          <h2>{group}</h2>
          <div class="fields-grid">
            {#each keys as key}{#if key !== 'meshPath' || config.arena === 'accurate'}<label
                  class="field"
                  ><span>{label(key)}</span>{#if schema[key]?.enum}<select bind:value={config[key]}
                      >{#each schema[key]?.enum ?? [] as option}<option value={option}
                          >{option}</option
                        >{/each}</select
                    >{:else if schema[key]?.type === 'string'}<input
                      bind:value={config[key]}
                    />{:else}<input
                      type="number"
                      value={Number(config[key])}
                      min={schema[key]?.minimum}
                      max={schema[key]?.maximum}
                      step={schema[key]?.type === 'integer' ? 1 : 'any'}
                      oninput={(event) => (config[key] = event.currentTarget.valueAsNumber)}
                    />{/if}<small>{schema[key]?.description}</small></label
                >{/if}{/each}
          </div>
        </section>{/each}
    {:else if page === 'Rewards'}
      <div class="section-intro">
        <h2>Give progress a direction.</h2>
        <p>
          Dense terms pay per second. Touches and goals pay on events. A high reward is evidence of
          optimizing this recipe, not proof of a strong player.
        </p>
        <button class="primary" onclick={applyRewards}>Apply reward weights</button>
      </div>
      <div class="reward-table">
        <div class="reward-header">
          <span>BEHAVIOR</span><span>WEIGHT</span><span>WHAT IT ENCOURAGES</span>
        </div>
        {#each rewardKeys as key}<div class="reward-row">
            <label for={'reward-' + key}
              >{label(key)}<small
                >{['touch', 'goal', 'boostPickup'].includes(key)
                  ? 'Event reward'
                  : 'Rate × elapsed seconds'}</small
              ></label
            ><input
              id={'reward-' + key}
              type="number"
              step="any"
              min={schema[key]?.minimum}
              max={schema[key]?.maximum}
              value={Number(config[key])}
              oninput={(event) => (config[key] = event.currentTarget.valueAsNumber)}
            />
            <p>{schema[key]?.description}</p>
          </div>{/each}
      </div>
    {:else if page === 'Match'}
      <div class="toolbar checkpoint-controls">
        <label
          >Blue checkpoint<select bind:value={checkpoint}
            ><option value="">Select a model</option>{#each checkpoints as cp}<option
                value={cp.path}>{cp.name}</option
              >{/each}</select
          ></label
        ><label
          >Orange checkpoint<select bind:value={opponent}
            ><option value="">Same model as blue</option>{#each checkpoints as cp}<option
                value={cp.path}>{cp.name}</option
              >{/each}</select
          ></label
        ><label>Matches<input type="number" min="1" max="100000" bind:value={matchCount} /></label
        ><button disabled={active || !checkpoint} onclick={() => start('play')}>Watch match</button
        ><button disabled={active || !checkpoint || !opponent} onclick={() => start('eval')}
          >Evaluate</button
        >
      </div>
      <MatchView {frame} large={true} />{#if evaluation}<div class="alert notice" role="status">
          {evaluation}
        </div>{/if}
      <p class="help-note">
        Each evaluation match ends at the first goal or the saved episode time limit. A time limit
        is a draw. Compare equal settings over many matches; these results are not ranked-game
        ratings.
      </p>
    {:else if page === 'Runs'}
      <div class="toolbar">
        <button onclick={() => safely(refreshRuns)}>Refresh library</button><label
          >Compare run<select bind:value={comparePath}
            ><option value="">None</option>{#each runs as run}<option value={run.path}
                >{run.config.name} · {run.id}</option
              >{/each}</select
          ></label
        ><button onclick={compare}>Load comparison</button>
      </div>
      {#if runs.length === 0}<div class="empty-state">
          <span class="empty-symbol">↗</span>
          <h2>Your first experiment starts here.</h2>
          <p>Train a policy to create metrics and checkpoints. They will stay on this machine.</p>
          <button onclick={() => (page = 'Train')}>Configure a run</button>
        </div>{/if}
      {#each runs as run}<section class="run-row">
          <div>
            <small>{run.id}</small>
            <h2>{String(run.config.name)}</h2>
            <p>
              {run.config.teamSize}v{run.config.teamSize} · {run.config.arena} · {run.summary
                .status} · {run.checkpoints.length} checkpoints
            </p>
          </div>
          <div class="run-actions">
            <button disabled={active} onclick={() => inspect(run)}>View metrics</button><button
              disabled={active || !run.checkpoints.length}
              onclick={() => {
                checkpoint = run.checkpoints.at(-1)?.path ?? '';
                page = 'Match';
              }}>Watch latest</button
            ><button
              disabled={active || !run.checkpoints.length}
              onclick={() => {
                checkpoint = run.checkpoints.at(-1)?.path ?? '';
                config = { ...run.config };
                void start('train', true);
              }}>Resume latest</button
            >
          </div>
        </section>{/each}
    {:else if page === 'Assistant'}
      <div class="assistant-layout">
        <section class="assistant-compose">
          <div class="eyebrow">EXPERIMENT PARTNER</div>
          <h2>Make the next change<br />an informed one.</h2>
          <p>
            Ask about the current configuration and the last 30 updates. The AI receives that
            context only when you send a question. Suggestions are reviewed before application.
          </p>
          <label for="question">Your question</label><textarea
            id="question"
            bind:value={question}
            rows="5"></textarea>
          <div class="toolbar">
            <button class="primary" disabled={asking || !model || !native} onclick={ask}
              >{asking ? 'Analyzing…' : 'Ask AI assistant'}</button
            ><button onclick={localReview}>Local diagnostic</button>
          </div>
          <small
            >The local diagnostic uses fixed rules and works without an API key. AI requests use
            your OpenAI API account and may incur charges.</small
          >
        </section>
        <section class="proposal-panel">
          <div class="panel-heading">
            <h3>Suggested experiment</h3>
            <span>{Object.keys(proposal?.changes ?? {}).length} changes</span>
          </div>
          {#if proposal}<p class="proposal-text" data-selectable>{proposal.explanation}</p>
            {#each Object.entries(proposal.changes) as [key, value]}<div class="proposal-change">
                <span>{label(key)}</span><del>{config[key]}</del><strong>{value}</strong>
              </div>{/each}{#if Object.keys(proposal.changes).length}<button
                class="primary"
                onclick={applyProposal}>Apply reviewed changes</button
              >{/if}{:else}<p class="muted">
              A proposal will appear here with the explanation and exact setting changes.
            </p>{/if}
        </section>
      </div>
    {:else if page === 'Learn'}
      <div class="docs-layout">
        <aside class="docs-nav">
          <input
            aria-label="Search documentation"
            placeholder="Search the handbook"
            bind:value={docSearch}
          />{#each filteredDocs as key}<button
              class:selected={key === docKey}
              onclick={() => (docKey = key)}
              >{key.split('/').at(-1)?.replace('.md', '').replaceAll('-', ' ')}</button
            >{/each}
        </aside>
        <Document onLink={openDoc} text={documents[docKey] ?? 'Choose a topic to start reading.'} />
      </div>
    {:else if page === 'Settings'}
      <section class="settings-section">
        <h2>Appearance</h2>
        <div class="theme-options">
          {#each [['forest', 'Forest', 'A cool green workspace'], ['midnight', 'Midnight', 'Deep blue and silver'], ['paper', 'Paper', 'A brighter reading space']] as choice}<button
              class:chosen={theme === choice[0]}
              onclick={() => (theme = choice[0] ?? 'forest')}
              ><span>{choice[1]}</span><small>{choice[2]}</small></button
            >{/each}
        </div>
      </section>
      <section class="settings-section">
        <h2>AI assistant</h2>
        <div class="fields-grid">
          <label class="field"
            ><span>OpenAI API key</span><input
              type="password"
              bind:value={apiKey}
              autocomplete="off"
              placeholder="Stored in Windows Credential Manager"
            /><small>Your key stays in the operating system credential vault.</small></label
          ><label class="field"
            ><span>Model ID</span><input
              bind:value={model}
              placeholder="Enter a Responses API model available to your account"
            /><small>Use an exact model ID from your OpenAI account.</small></label
          >
        </div>
        <button
          disabled={!apiKey || !native}
          onclick={() =>
            safely(async () => {
              await invoke('save_api_key', { key: apiKey });
              apiKey = '';
              notice = 'API key saved in Windows Credential Manager.';
            })}>Save API key</button
        ><button
          disabled={!native}
          onclick={() =>
            safely(async () => {
              await invoke('save_api_key', { key: '' });
              notice = 'Saved API key removed.';
            })}>Remove key</button
        >
      </section>
      <section class="settings-section">
        <h2>Engine diagnostics</h2>
        <p>
          {available
            ? 'Native engine found.'
            : 'Engine unavailable. Build with tools/build.ps1 and reopen the desktop app.'}
        </p>
        <button disabled={!available || active} onclick={() => start('bench')}
          >Run physics benchmark</button
        >{#if benchmark}<p>{benchmark}</p>{/if}
        <details>
          <summary>Engine log ({log.length} lines)</summary>
          <pre data-selectable>{log.join('\n') || 'No diagnostic output yet.'}</pre>
        </details>
      </section>
    {/if}
    <footer>RL STUDIO <span>Local simulation · Real measurements · Your experiments</span></footer>
  </main>
</div>

{#if onboarding}
  <div class="modal-backdrop">
    <div
      class="onboarding"
      use:focusDialog
      role="dialog"
      aria-modal="true"
      aria-labelledby="welcome-title"
      tabindex="-1"
    >
      <div class="eyebrow">WELCOME TO RL STUDIO</div>
      <h1 id="welcome-title">Teach a policy.<br /><em>Watch it find its feet.</em></h1>
      <p>
        A reinforcement learning agent starts with random actions. You define rewards, it gathers
        experience, and PPO turns that experience into a better policy.
      </p>
      <div class="arena-options">
        <button
          class:chosen={config.arena === 'practice'}
          onclick={() => (config.arena = 'practice')}
          ><strong>Practice Arena</strong><span>Ready immediately</span>
          <p>
            Generated, approximate geometry. No game files needed. The easiest place to learn.
          </p></button
        ><button
          class:chosen={config.arena === 'accurate'}
          onclick={() => (config.arena = 'accurate')}
          ><strong>Accurate Arena</strong><span>Bring your own meshes</span>
          <p>
            Uses geometry dumped from your installation. Configure the mesh path in Train.
          </p></button
        >
      </div>
      <p class="help-note">
        Models play inside this simulator. Learning useful skills takes time; strong play needs
        extensive training and evaluation.
      </p>
      <button
        class="primary"
        onclick={() => {
          onboarding = false;
          localStorage.setItem('rlstudio-onboarded', 'yes');
          page = 'Train';
        }}>Enter the workspace →</button
      ><button
        class="text-button"
        onclick={() => {
          onboarding = false;
          page = 'Learn';
        }}>Read the handbook first</button
      >
    </div>
  </div>
{/if}
