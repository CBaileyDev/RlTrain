# UI regression checks

1. Run `npm --prefix app run dev -- --host 127.0.0.1`.
2. Open `playwright-cli -s=rlstudio open http://127.0.0.1:1420/`.
3. Complete onboarding with Enter the workspace.
4. Run `playwright-cli -s=rlstudio run-code --filename=tools/tests/ui-smoke.js`.

The script checks navigation, reward editing, offline diagnostic labeling, Markdown,
theme persistence, empty library/playback states and layout at 1100×700. It writes
screenshots under engine/build. It requires a fresh browser profile with no run data.

This is a browser-preview check. It does not claim to test the native Tauri bridge.
Native process supervision is compiled with cargo; tools/test-engine.py tests the actual
engine protocol and training lifecycle. Native WebView end-to-end inspection was unavailable
in the development environment because automatic approval review blocked a debug-port launch.


## Workbench improvements

With the same development server running, execute each fixture in its own browser session:

```powershell
playwright-cli -s=workbench open http://127.0.0.1:1420/
playwright-cli -s=workbench run-code --filename=tools/tests/workbench-smoke.js
playwright-cli -s=viewer open http://127.0.0.1:1420/
playwright-cli -s=viewer run-code --filename=tools/tests/viewer-smoke.js
node --experimental-strip-types --test tools/tests/metrics.test.mjs
```

These browser fixtures mock the Tauri bridge **only inside the tests**. They verify populated
charts, the 5-billion-step restored display before the first update, automatic AI context,
live reward versus staged optimizer changes, stale-response rejection, and relative skill.
The viewer fixture checks moving frames, car selection, demolition, teleports, fullscreen,
stale data, and laptop layout. Images are written under `engine/build/`.
They complement the actual engine integration suite and actual paid NeoToken Rust test;
they do not claim to be native WebView end-to-end tests.
