/// <reference types="svelte" />
/// <reference types="vite/client" />

// Tauri injects this object into the WebView before any app script runs. Its absence is
// how we detect "running in a plain browser via `npm run dev`" and skip native calls.
interface Window {
  readonly __TAURI_INTERNALS__?: unknown;
}
