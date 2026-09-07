import { vitePreprocess } from "@sveltejs/vite-plugin-svelte";

/** @type {import('@sveltejs/vite-plugin-svelte').SvelteConfig} */
export default {
  // Compiles `<script lang="ts">` blocks. It strips types only — no type checking here;
  // that is `npm run check`'s job.
  preprocess: vitePreprocess(),

  compilerOptions: {
    // Force runes mode project-wide rather than letting the compiler infer it per file.
    // Inference means a component that happens to use no runes silently falls back to the
    // legacy reactivity model, and legacy `export let` props keep working by accident.
    // This makes that a compile error instead.
    runes: true,
  },
};
