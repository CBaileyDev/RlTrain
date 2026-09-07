//! RL Studio — Tauri backend.
//!
//! This crate is the desktop shell's native half. Its eventual jobs are: supervise the
//! `rl-engine.exe` child process, relay its local WebSocket to the frontend, read and
//! write run configuration, and hold the assistant's API key in the OS credential vault.
//!
//! Right now it is the skeleton: the builder, the plugins, and one command that proves
//! the IPC bridge round-trips.

mod error;

pub use error::{AppError, AppResult};

use serde::Serialize;

/// Identity of the running build, reported to the frontend on startup.
///
/// `camelCase` because this crosses into TypeScript, where snake_case fields read as
/// foreign. The mirror of this struct is `AppInfo` in `app/src/lib/types.ts`.
#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct AppInfo {
    pub name: String,
    pub version: String,
    pub tauri_version: String,
}

/// Placeholder command. Also the frontend's liveness check on the native bridge: if this
/// fails, nothing else built on IPC is going to work either, and the UI can say so
/// plainly instead of failing screen by screen.
#[tauri::command]
fn app_info(app: tauri::AppHandle) -> AppResult<AppInfo> {
    let package = app.package_info();
    Ok(AppInfo {
        name: package.name.clone(),
        version: package.version.to_string(),
        tauri_version: tauri::VERSION.to_string(),
    })
}

/// Build and run the desktop application.
///
/// Separated from `main` so the mobile entry points and integration tests can call it.
pub fn run() {
    tauri::Builder::default()
        // Persists user settings (theme, window state, onboarding progress) to a JSON
        // file in the OS app-data directory, so they survive an app update.
        .plugin(tauri_plugin_store::Builder::new().build())
        // Opens documentation links and run folders in the user's own browser/explorer.
        // The frontend has no other way to reach outside the WebView, by design.
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![app_info])
        // `expect` rather than `unwrap`: this is startup, not a command handler, and a
        // failure here means the app genuinely cannot exist. The message is what the
        // user would see in a crash report, so it needs to say something.
        .run(tauri::generate_context!())
        .expect("RL Studio: failed to start the Tauri application");
}
