<script lang="ts">
  import { onMount } from 'svelte';
  import uPlot from 'uplot';
  import 'uplot/dist/uPlot.min.css';
  import type { Metric } from './types';
  let {
    rows,
    field,
    title,
    color = '#6bd5ba',
  }: { rows: Metric[]; field: keyof Metric; title: string; color?: string } = $props();
  let container: HTMLDivElement;
  let chart: uPlot | undefined;
  onMount(() => {
    chart = new uPlot(
      {
        width: container.clientWidth,
        height: 185,
        cursor: { drag: { x: true, y: false } },
        legend: { show: false },
        scales: { x: { time: false } },
        series: [{ label: 'Agent steps' }, { label: title, stroke: color, width: 2 }],
        axes: [
          {
            stroke: '#8899a4',
            grid: { stroke: '#25343d' },
            font: '11px Segoe UI',
            values: (_u, values) =>
              values.map((v) => (v >= 1000 ? `${(v / 1000).toFixed(0)}k` : String(v))),
          },
          { stroke: '#8899a4', grid: { stroke: '#25343d' }, font: '11px Segoe UI', size: 58 },
        ],
      },
      [rows.map((r) => r.steps), rows.map((r) => r[field])],
      container,
    );
    const observer = new ResizeObserver(() =>
      chart?.setSize({ width: container.clientWidth, height: 185 }),
    );
    observer.observe(container);
    return () => {
      observer.disconnect();
      chart?.destroy();
      chart = undefined;
    };
  });
  $effect(() => {
    chart?.setData([rows.map((r) => r.steps), rows.map((r) => r[field])]);
  });
</script>

<section class="chart-panel">
  <div class="panel-heading">
    <h3>{title}</h3>
    <span>by agent steps</span>
  </div>
  <div class="chart" bind:this={container}></div>
  {#if rows.length === 0}<p class="chart-empty">
      Measurements appear after the first PPO update.
    </p>{/if}
</section>
