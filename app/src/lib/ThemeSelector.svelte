<script lang="ts">
  import { themes } from '../themes';

  let { value = 'forest', onChange }: { value?: string; onChange?: (theme: string) => void } = $props();

  const darkThemes = $derived(themes.filter((t) => t.category === 'dark'));
  const lightThemes = $derived(themes.filter((t) => t.category === 'light'));

  function selectTheme(themeId: string) {
    if (onChange) onChange(themeId);
  }
</script>

<div class="theme-selector">
  <div class="theme-category">
    <h3>Dark Themes</h3>
    <div class="theme-grid">
      {#each darkThemes as theme}
        <button
          class="theme-card"
          class:selected={value === theme.id}
          onclick={() => selectTheme(theme.id)}
          type="button"
        >
          <div class="theme-preview" style="--preview-bg: {theme.colors.bg}; --preview-panel: {theme.colors.panel}; --preview-accent: {theme.colors.accent}; --preview-border: {theme.colors.border};">
            <div class="preview-header" style="background: {theme.colors.panel}; border-bottom: 1px solid {theme.colors.border};">
              <div class="preview-dot" style="background: {theme.colors.accent};"></div>
              <div class="preview-dot" style="background: {theme.colors.accent};"></div>
              <div class="preview-dot" style="background: {theme.colors.accent};"></div>
            </div>
            <div class="preview-body" style="background: {theme.colors.bg};">
              <div class="preview-sidebar" style="background: {theme.colors.sidebar}; border-right: 1px solid {theme.colors.border};"></div>
              <div class="preview-content">
                <div class="preview-bar" style="background: {theme.colors.elevated}; border: 1px solid {theme.colors.border};"></div>
                <div class="preview-bar" style="background: {theme.colors.elevated}; border: 1px solid {theme.colors.border};"></div>
                <div class="preview-bar short" style="background: {theme.colors.accent};"></div>
              </div>
            </div>
          </div>
          <div class="theme-info">
            <span class="theme-name">{theme.name}</span>
            <small>{theme.description}</small>
          </div>
          {#if value === theme.id}
            <div class="selected-badge">✓</div>
          {/if}
        </button>
      {/each}
    </div>
  </div>

  <div class="theme-category">
    <h3>Light Themes</h3>
    <div class="theme-grid">
      {#each lightThemes as theme}
        <button
          class="theme-card"
          class:selected={value === theme.id}
          onclick={() => selectTheme(theme.id)}
          type="button"
        >
          <div class="theme-preview" style="--preview-bg: {theme.colors.bg}; --preview-panel: {theme.colors.panel}; --preview-accent: {theme.colors.accent}; --preview-border: {theme.colors.border};">
            <div class="preview-header" style="background: {theme.colors.panel}; border-bottom: 1px solid {theme.colors.border};">
              <div class="preview-dot" style="background: {theme.colors.accent};"></div>
              <div class="preview-dot" style="background: {theme.colors.accent};"></div>
              <div class="preview-dot" style="background: {theme.colors.accent};"></div>
            </div>
            <div class="preview-body" style="background: {theme.colors.bg};">
              <div class="preview-sidebar" style="background: {theme.colors.sidebar}; border-right: 1px solid {theme.colors.border};"></div>
              <div class="preview-content">
                <div class="preview-bar" style="background: {theme.colors.elevated}; border: 1px solid {theme.colors.border};"></div>
                <div class="preview-bar" style="background: {theme.colors.elevated}; border: 1px solid {theme.colors.border};"></div>
                <div class="preview-bar short" style="background: {theme.colors.accent};"></div>
              </div>
            </div>
          </div>
          <div class="theme-info">
            <span class="theme-name">{theme.name}</span>
            <small>{theme.description}</small>
          </div>
          {#if value === theme.id}
            <div class="selected-badge">✓</div>
          {/if}
        </button>
      {/each}
    </div>
  </div>
</div>

<style>
  .theme-selector {
    display: flex;
    flex-direction: column;
    gap: 32px;
  }

  .theme-category h3 {
    margin-bottom: 16px;
    font-size: 16px;
    font-weight: 600;
    color: var(--text);
  }

  .theme-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
    gap: 16px;
  }

  .theme-card {
    display: flex;
    flex-direction: column;
    padding: 0;
    border: 2px solid var(--border);
    border-radius: var(--radius-lg);
    background: var(--panel);
    cursor: pointer;
    transition: all 200ms cubic-bezier(0.4, 0, 0.2, 1);
    position: relative;
    overflow: hidden;
    text-align: left;
  }

  .theme-card:hover {
    border-color: var(--accent);
    transform: translateY(-4px);
    box-shadow: 0 10px 20px rgba(0, 0, 0, 0.15);
  }

  .theme-card.selected {
    border-color: var(--accent);
    box-shadow: 0 0 0 3px color-mix(in srgb, var(--accent) 20%, transparent);
  }

  .theme-preview {
    width: 100%;
    height: 120px;
    border-radius: var(--radius-md) var(--radius-md) 0 0;
    overflow: hidden;
    display: flex;
    flex-direction: column;
  }

  .preview-header {
    display: flex;
    gap: 6px;
    padding: 10px;
    align-items: center;
  }

  .preview-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
  }

  .preview-body {
    flex: 1;
    display: flex;
    gap: 8px;
    padding: 8px;
  }

  .preview-sidebar {
    width: 30px;
    border-radius: 4px;
  }

  .preview-content {
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .preview-bar {
    height: 12px;
    border-radius: 3px;
  }

  .preview-bar.short {
    width: 60%;
    height: 8px;
  }

  .theme-info {
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .theme-name {
    font-weight: 600;
    font-size: 14px;
    color: var(--text);
  }

  .theme-card small {
    font-size: 11px;
    color: var(--muted);
    line-height: 1.4;
  }

  .selected-badge {
    position: absolute;
    top: 12px;
    right: 12px;
    width: 28px;
    height: 28px;
    border-radius: 50%;
    background: var(--accent);
    color: var(--accent-ink);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 16px;
    font-weight: bold;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.2);
    animation: popIn 200ms cubic-bezier(0.4, 0, 0.2, 1);
  }

  @keyframes popIn {
    0% {
      transform: scale(0);
      opacity: 0;
    }
    50% {
      transform: scale(1.1);
    }
    100% {
      transform: scale(1);
      opacity: 1;
    }
  }
</style>
