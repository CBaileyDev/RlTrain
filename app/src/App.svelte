<script lang="ts">
  // PLACEHOLDER. A later phase replaces this with the real app shell: custom title bar,
  // navigation rail, router and the Dashboard / Train / Rewards / Match / Runs /
  // Assistant / Docs / Settings screens.
  //
  // It exists now only to prove the whole chain works end to end: Svelte 5 runes compile,
  // TypeScript type-checks, Vite builds, and the Tauri IPC round-trips to Rust.

  import { onMount } from "svelte";
  import type { AppInfo } from "./lib/types";

  // `$state` is Svelte 5's rune replacement for a writable store. Reassigning it
  // re-renders whatever read it, with no subscription bookkeeping.
  let info = $state<AppInfo | null>(null);
  let error = $state<string | null>(null);

  onMount(async () => {
    // Running under `npm run dev` in a browser there is no Rust side to talk to.
    if (!("__TAURI_INTERNALS__" in window)) {
      error = "Not running inside Tauri — native bridge unavailable.";
      return;
    }
    try {
      const { invoke } = await import("@tauri-apps/api/core");
      info = await invoke<AppInfo>("app_info");
    } catch (cause) {
      error = cause instanceof Error ? cause.message : String(cause);
    }
  });

  const status = $derived(
    info ? `${info.name} ${info.version} · Tauri ${info.tauriVersion}` : (error ?? "Connecting…"),
  );
</script>

<main>
  <h1>RL Studio</h1>
  <p class="tagline">Desktop workbench for learning reinforcement learning.</p>
  <p class="status" class:error={error !== null}>{status}</p>
  <p class="note">Skeleton only. The design system and app shell arrive in a later phase.</p>
</main>

<style>
  main {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 8px;
    height: 100%;
    padding: 32px;
    text-align: center;
  }

  h1 {
    font-size: 28px;
    font-weight: 600;
    letter-spacing: -0.02em;
  }

  .tagline {
    color: #9aa3b8;
  }

  .status {
    margin-top: 16px;
    padding: 6px 12px;
    border: 1px solid #232a3a;
    border-radius: 999px;
    font-size: 12px;
    color: #5b8cff;
  }

  .status.error {
    color: #f0a35b;
  }

  .note {
    font-size: 12px;
    color: #5a6478;
  }
</style>
