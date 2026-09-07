mod assistant;
mod engine;
mod error;
pub use error::{AppError, AppResult};
use std::sync::atomic::Ordering;
use tauri::{Emitter, Manager};
pub fn run() {
    tauri::Builder::default()
        .manage(engine::EngineState::default())
        .plugin(tauri_plugin_store::Builder::new().build())
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![
            engine::environment,
            engine::start_engine,
            engine::engine_control,
            engine::list_runs,
            engine::run_metrics,
            engine::save_api_key,
            assistant::ask_assistant,
            assistant::assistant_settings
        ])
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                let state = window.state::<engine::EngineState>();
                if state.active.load(Ordering::SeqCst) {
                    api.prevent_close();
                    if state.closing.swap(true, Ordering::SeqCst) {
                        return;
                    }
                    if let Ok(sender) = state.sender.lock() {
                        if let Some(sender) = sender.as_ref() {
                            let _ = sender.try_send(serde_json::json!({"type":"stop"}));
                        }
                    }
                    let _ = window.emit(
                        "engine-event",
                        serde_json::json!({"type":"status","status":"stopping"}),
                    );
                    let window = window.clone();
                    tauri::async_runtime::spawn(async move {
                        let mut ticks = 0;
                        while window
                            .state::<engine::EngineState>()
                            .active
                            .load(Ordering::SeqCst)
                        {
                            tokio::time::sleep(std::time::Duration::from_millis(100)).await;
                            ticks += 1;
                            if ticks == 300 {
                                // A stalled child must not keep the app alive forever.
                                // Closing the channel asks its owning task to kill it.
                                if let Ok(mut sender) =
                                    window.state::<engine::EngineState>().sender.lock()
                                {
                                    sender.take();
                                }
                            }
                        }
                        let _ = window.close();
                    });
                }
            }
            if let tauri::WindowEvent::Destroyed = event {
                let state = window.state::<engine::EngineState>();
                if let Ok(mut sender) = state.sender.lock() {
                    sender.take();
                };
            }
        })
        .run(tauri::generate_context!())
        .expect("RL Studio: failed to start the desktop application");
}
