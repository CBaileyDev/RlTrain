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
