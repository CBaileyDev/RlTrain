import { mount } from "svelte";
import App from "./App.svelte";
import "./app.css";

const target = document.getElementById("app");
if (!target) {
  // index.html is ours, so this can only happen if the shell was edited badly. Failing
  // loudly here is far easier to debug than a silently blank window.
  throw new Error('RL Studio: mount target #app is missing from index.html');
}

const app = mount(App, { target });

/**
 * Reveal the native window.
 *
 * tauri.conf.json creates the main window with `visible: false`. WebView2 paints white
 * before the first frame of our own CSS lands, and on a frameless window that reads as a
 * flash of a broken app. So the Rust side never shows the window — the frontend does,
 * once it has actually rendered something.
 *
 * The import is dynamic because `npm run dev` in a plain browser has no Tauri IPC; a
 * static import would pull the module in and throw at load time.
 */
async function revealWindow(): Promise<void> {
  if (!("__TAURI_INTERNALS__" in window)) return;

  // One rAF schedules us for the next frame; the second fires after that frame has been
  // committed. Showing the window inside the first would still race the paint.
  await new Promise<void>((resolve) =>
    requestAnimationFrame(() => requestAnimationFrame(() => resolve())),
  );

  try {
    const { getCurrentWindow } = await import("@tauri-apps/api/window");
    await getCurrentWindow().show();
  } catch (error) {
    // A window we cannot show is a window the user can never close, so this failure must
    // never be swallowed silently. A later phase routes this into the error surface.
    console.error("RL Studio: failed to reveal the main window", error);
  }
}

void revealWindow();

export default app;
