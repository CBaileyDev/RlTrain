import { defineConfig } from "vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";

// Tauri sets this when you run `tauri dev --host` to develop against another machine on
// the LAN. It is absent for the normal desktop flow, which is what we care about.
const host = process.env["TAURI_DEV_HOST"];
const isDebugBuild = Boolean(process.env["TAURI_ENV_DEBUG"]);

// https://vite.dev/config/
export default defineConfig({
  plugins: [svelte()],

  // Tauri prints cargo's output to the same terminal. Letting Vite clear the screen
  // would erase Rust compiler errors right as the user needs to read them.
  clearScreen: false,

  server: {
    // tauri.conf.json hard-codes devUrl to this port, so falling back to another one
    // would leave the desktop window pointing at nothing. Fail loudly instead.
    port: 1420,
    strictPort: true,
    host: host ?? false,
    // Spread rather than `hmr: host ? {...} : undefined` because tsconfig sets
    // exactOptionalPropertyTypes, under which an explicit `undefined` is not the same
    // as an absent key.
    ...(host ? { hmr: { protocol: "ws" as const, host, port: 1421 } } : {}),
    watch: {
      // src-tauri/target churns thousands of files during a cargo build. Watching it
      // pins a CPU core and triggers pointless frontend reloads.
      ignored: ["**/src-tauri/**"],
    },
  },

  build: {
    // The frontend only ever runs inside WebView2 (Chromium), so target it directly:
    // no legacy transpilation and no polyfills we would never use.
    target: "chrome120",
    minify: isDebugBuild ? false : "esbuild",
    sourcemap: isDebugBuild,
  },
});
