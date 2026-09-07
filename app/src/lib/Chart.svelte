<script lang="ts">
  import { onMount } from 'svelte';
  import uPlot from 'uplot';
  import 'uplot/dist/uPlot.min.css';
  import { metricSeries } from './metrics';
  import type { Metric } from './types';
  let { rows, field, title, color = '#6bd5ba', smoothing = 0.8, height = 220, comparison = [], description = '' }:
    { rows: Metric[]; field: keyof Metric; title: string; color?: string; smoothing?: number; height?: number; comparison?: Metric[]; description?: string } = $props();
  let container: HTMLDivElement;
  let chart: uPlot | undefined;
  let zoomed = $state(false);
  let hover = $state('');
  const series = $derived(metricSeries(rows, field, smoothing));
  const other = $derived(metricSeries(comparison, field, smoothing));
  function data(): uPlot.AlignedData {
    const steps = [...new Set([...series.steps, ...other.steps])].sort((a, b) => a - b);
    const own = new Map(series.steps.map((s, i) => [s, i]));
    const compared = new Map(other.steps.map((s, i) => [s, i]));
    return [steps,
      steps.map(s => own.has(s) ? series.raw[own.get(s)!] : null),
      steps.map(s => own.has(s) ? series.smooth[own.get(s)!] : null),
      steps.map(s => compared.has(s) ? other.smooth[compared.get(s)!] : null)];
  }
  function format(value: number | null | undefined) {
    return value == null ? '—' : value.toLocaleString(undefined, { maximumSignificantDigits: 5 });
  }
  function resetZoom() { zoomed = false; chart?.setData(data()); }
  onMount(() => {
    const css = getComputedStyle(container);
    const axis = css.getPropertyValue('--muted').trim() || '#8899a4';
    const grid = css.getPropertyValue('--border').trim() || '#25343d';
    chart = new uPlot({
      width: Math.max(240, container.clientWidth), height,
      cursor: { drag: { x: true, y: false }, sync: { key: 'rlstudio-metrics' } },
      legend: { show: false }, scales: { x: { time: false } },
      series: [{label:'Agent steps'},
        {label:'Raw',stroke:axis,width:1,alpha:0.38,points:{show:rows.length < 3}},
        {label:'Trend',stroke:color,width:2,points:{show:rows.length < 3},spanGaps:true},
        {label:'Comparison',stroke:'#cf9b57',width:2,dash:[5,4],spanGaps:true}],
      axes: [
        {stroke:axis,grid:{stroke:grid},font:'11px Segoe UI',values:(_u, values)=>values.map(v=>v.toLocaleString('en-US',{notation:'compact',maximumSignificantDigits:7}))},
        {stroke:axis,grid:{stroke:grid},font:'11px Segoe UI',size:65,values:(_u,values)=>values.map(v=>format(v))}],
      hooks: {
        setSelect: [(u) => { if (u.select.width > 0) zoomed = true; }],
        setCursor: [(u) => {
          const i = u.cursor.idx;
          hover = i == null ? '' : `Step ${format(u.data[0][i])} · raw ${format(u.data[1]?.[i])} · trend ${format(u.data[2]?.[i])}${comparison.length ? ` · comparison ${format(u.data[3]?.[i])}` : ''}`;
        }],
      },
    }, data(), container);
    const observer = new ResizeObserver(() => chart?.setSize({width:Math.max(240,container.clientWidth),height}));
    observer.observe(container);
    return () => { observer.disconnect(); chart?.destroy(); chart = undefined; };
  });
  $effect(() => {
    const next = data();
    if (chart) {
      const range = {min:chart.scales.x?.min, max:chart.scales.x?.max};
      chart.setData(next, !zoomed);
      if (zoomed && range.min !== undefined && range.max !== undefined) {
        chart.setScale('x', {min:range.min,max:range.max});
        chart.redraw();
      }
    }
  });
</script>

<section class="chart-panel" aria-label={title}>
  <div class="panel-heading"><h3>{title}</h3><span>{format(series.raw.at(-1))}</span></div>
  <p class="chart-caption">{description || 'Per PPO update'} · cumulative agent steps</p>
  <div class="chart" style:height={`${height}px`} bind:this={container}></div>
  {#if rows.length === 0}<p class="chart-empty">Measurements appear after the first PPO update.</p>{/if}
  <div class="chart-detail"><span>{hover || `${series.steps.length} updates · ${smoothing ? `EMA ${smoothing.toFixed(2)} + raw trace` : 'Raw measurements'}${comparison.length ? ' · dashed comparison' : ''}`}</span><button onclick={resetZoom}>{zoomed ? 'Reset zoom' : 'Fit data'}</button></div>
</section>

<style>
  .chart-caption { color:var(--muted); font-size:11px; margin:0 20px 12px; }
  .chart-detail { display:flex; align-items:center; gap:8px; justify-content:space-between; min-height:43px; padding:5px 16px 10px; color:var(--muted); font-size:10px; }
  .chart-detail button { font-size:10px; padding:4px 8px; white-space:nowrap; }
  .chart-detail span { font-variant-numeric:tabular-nums; }
</style>
