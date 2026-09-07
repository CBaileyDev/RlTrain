//! Native process supervision and filesystem bridge. No shell command interpolation.
use crate::{AppError, AppResult};
use serde_json::{json, Value};
use std::{
    path::{Path, PathBuf},
    process::Stdio,
    sync::{
        atomic::{AtomicBool, Ordering},
        Mutex,
    },
};
use tauri::{Emitter, State};
use tokio::{
    io::{AsyncBufReadExt, AsyncWriteExt, BufReader},
    process::Command,
    sync::mpsc,
};

#[derive(Default)]
pub struct EngineState {
    pub active: AtomicBool,
    pub closing: AtomicBool,
    pub sender: Mutex<Option<mpsc::Sender<Value>>>,
}
fn root() -> PathBuf {
    if let Some(path) = std::env::var_os("RL_STUDIO_HOME") {
        return PathBuf::from(path);
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(parent) = exe.parent() {
            if parent.join("engine/build/bin/rl-engine.exe").exists() {
                return parent.to_owned();
            }
        }
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .canonicalize()
        .unwrap_or_else(|_| PathBuf::from("."))
}
fn engine() -> PathBuf {
    root().join("engine/build/bin/rl-engine.exe")
}
fn load(path: &Path) -> AppResult<Value> {
    Ok(serde_json::from_str(&std::fs::read_to_string(path)?)?)
}
pub(crate) fn validate(config: &Value) -> AppResult<()> {
    let schema: Value =
        serde_json::from_str(include_str!("../../../configs/schema/run.schema.json"))?;
    let object = config
        .as_object()
        .ok_or_else(|| AppError::Config("Configuration must be an object".into()))?;
    for (key, value) in object {
        let rule = &schema["properties"][key];
        let valid = match rule["type"].as_str() {
            Some("integer") => value.is_i64() || value.is_u64(),
            Some("number") => value.is_number(),
            Some("string") => value.is_string(),
            Some("boolean") => value.is_boolean(),
            _ => false,
        };
        if !valid {
            return Err(AppError::Config(format!("Invalid setting: {key}")));
        }
        if let Some(choices) = rule["enum"].as_array() {
            if !choices.contains(value) {
                return Err(AppError::Config(format!("Unsupported value: {key}")));
            }
        }
        if let Some(n) = value.as_f64() {
            if !n.is_finite()
                || rule["minimum"].as_f64().is_some_and(|min| n < min)
                || rule["maximum"].as_f64().is_some_and(|max| n > max)
            {
                return Err(AppError::Config(format!("Out of range: {key}")));
            }
        }
    }
    Ok(())
}
#[tauri::command]
pub fn environment() -> Value {
    json!({"engineAvailable": engine().exists(), "root": root(), "active": false})
}
#[tauri::command]
pub async fn start_engine(
    app: tauri::AppHandle,
    state: State<'_, EngineState>,
    mode: String,
    config: Value,
    checkpoint: Option<String>,
    opponent: Option<String>,
    matches: Option<u32>,
) -> AppResult<()> {
    if !["train", "play", "eval", "bench"].contains(&mode.as_str()) {
        return Err(AppError::Config("Unknown mode".into()));
    }
    validate(&config)?;
    if !engine().is_file() {
        return Err(AppError::Engine(
            "Engine is missing. Run tools/build.ps1 first.".into(),
        ));
    }
    if state.closing.load(Ordering::SeqCst) {
        return Err(AppError::Engine(
            "The app is closing; wait for the current save to finish.".into(),
        ));
    }
    if state
        .active
        .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
        .is_err()
    {
        return Err(AppError::Engine(
            "A run is already active. Stop it before starting another.".into(),
        ));
    }
    let result: AppResult<_> = (|| {
        let mut command = Command::new(engine());
        command
            .current_dir(root())
            .arg(&mode)
            .arg("--interactive")
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .kill_on_drop(true);
        #[cfg(windows)]
        command.creation_flags(0x08000000);
        if mode == "train" || mode == "bench" {
            let stamp = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_err(|e| AppError::Internal(e.to_string()))?
                .as_nanos();
            let staging = root().join("runs/.configs");
            std::fs::create_dir_all(&staging)?;
            let path = staging.join(format!("{stamp}.json"));
            std::fs::write(&path, serde_json::to_vec_pretty(&config)?)?;
            command.arg("--config").arg(path);
            if mode == "train" {
                command
                    .arg("--run")
                    .arg(root().join(format!("runs/run-{stamp}")));
            }
            if mode == "train" {
                if let Some(cp) = checkpoint.as_ref().filter(|s| !s.is_empty()) {
                    command.arg("--checkpoint").arg(cp);
                }
            }
        } else {
            let cp = checkpoint
                .as_ref()
                .filter(|s| !s.is_empty())
                .ok_or_else(|| AppError::Config("Choose a checkpoint".into()))?;
            command
                .arg(if mode == "eval" {
                    "--a"
                } else {
                    "--checkpoint"
                })
                .arg(cp);
            if let Some(other) = opponent.as_ref().filter(|s| !s.is_empty()) {
                command
                    .arg(if mode == "eval" { "--b" } else { "--opponent" })
                    .arg(other);
            } else if mode == "eval" {
                return Err(AppError::Config("Choose an opponent".into()));
            }
            command
                .arg("--matches")
                .arg(matches.unwrap_or(20).to_string());
        }
        Ok(command.spawn()?)
    })();
    let mut child = match result {
        Ok(c) => c,
        Err(e) => {
            state.active.store(false, Ordering::SeqCst);
            return Err(e);
        }
    };
    let mut stdin = child
        .stdin
        .take()
        .ok_or_else(|| AppError::Engine("No engine stdin".into()))?;
    let stdout = child
        .stdout
        .take()
        .ok_or_else(|| AppError::Engine("No engine stdout".into()))?;
    let stderr = child
        .stderr
        .take()
        .ok_or_else(|| AppError::Engine("No engine stderr".into()))?;
    let (sender, mut receiver) = mpsc::channel::<Value>(32);
    *state
        .sender
        .lock()
        .map_err(|_| AppError::Internal("Engine state lock failed".into()))? = Some(sender);
    let output_app = app.clone();
    let output = tauri::async_runtime::spawn(async move {
        let mut lines = BufReader::new(stdout).lines();
        while let Ok(Some(line)) = lines.next_line().await {
            if let Ok(event) = serde_json::from_str::<Value>(&line) {
                let _ = output_app.emit("engine-event", event);
            }
        }
    });
    let log_app = app.clone();
    let logs = tauri::async_runtime::spawn(async move {
        let mut lines = BufReader::new(stderr).lines();
        while let Ok(Some(line)) = lines.next_line().await {
            let _ = log_app.emit("engine-log", line);
        }
    });
    tauri::async_runtime::spawn(async move {
        let exit = loop {
            tokio::select! {
                status = child.wait() => break status,
                command = receiver.recv() => {
                    if let Some(value) = command {
                        let text = format!("{}\n", value);
                        if stdin.write_all(text.as_bytes()).await.is_err() { break child.wait().await; }
                    } else { let _ = child.kill().await; break child.wait().await; }
                }
            }
        };
        let _ = output.await;
        let _ = logs.await;
        use tauri::Manager;
        let state = app.state::<EngineState>();
        state.active.store(false, Ordering::SeqCst);
        if let Ok(mut sender) = state.sender.lock() {
            *sender = None;
        }
        let code = exit.ok().and_then(|s| s.code()).unwrap_or(-1);
        let _ = app.emit("engine-event", json!({"type":"exit", "code":code}));
    });
    Ok(())
}
#[tauri::command]
pub async fn engine_control(state: State<'_, EngineState>, command: Value) -> AppResult<()> {
    if !["pause", "resume", "stop", "rewards"].contains(&command["type"].as_str().unwrap_or("")) {
        return Err(AppError::Config("Unknown control".into()));
    }
    let sender = state
        .sender
        .lock()
        .map_err(|_| AppError::Internal("Engine state lock failed".into()))?
        .clone()
        .ok_or_else(|| AppError::Engine("No active engine".into()))?;
    sender
        .send(command)
        .await
        .map_err(|_| AppError::Engine("Engine has exited".into()))
}
#[tauri::command]
pub fn list_runs() -> AppResult<Value> {
    let dir = root().join("runs");
    if !dir.exists() {
        return Ok(json!([]));
    }
    let mut result = Vec::new();
    for entry in std::fs::read_dir(dir)?.flatten() {
        let path = entry.path();
        if !path.join("config.json").exists() {
            continue;
        }
        let mut checkpoints = Vec::new();
        if let Ok(entries) = std::fs::read_dir(path.join("checkpoints")) {
            for cp in entries.flatten() {
                if cp.path().join("metadata.json").exists()
                    && !cp.file_name().to_string_lossy().ends_with(".pending")
                {
                    checkpoints.push(json!({"path":cp.path(), "metadata":load(&cp.path().join("metadata.json"))?}));
                }
            }
        }
        checkpoints.sort_by_key(|cp| cp["metadata"]["iteration"].as_u64().unwrap_or(0));
        result.push(json!({"path":path, "id":entry.file_name().to_string_lossy(), "config":load(&path.join("config.json"))?,
            "summary":load(&path.join("summary.json")).unwrap_or(json!({"status":"incomplete"})), "checkpoints":checkpoints}));
    }
    result.sort_by(|a, b| b["id"].as_str().cmp(&a["id"].as_str()));
    Ok(json!(result))
}
#[tauri::command]
pub fn run_metrics(path: String) -> AppResult<Value> {
    let path = PathBuf::from(path).canonicalize()?;
    if !path.starts_with(root().join("runs").canonicalize()?) {
        return Err(AppError::Config("Select a run from the library".into()));
    }
    let contents = std::fs::read_to_string(path.join("metrics.jsonl"))?;
    let rows: Vec<Value> = contents
        .lines()
        .filter_map(|s| serde_json::from_str(s).ok())
        .collect();
    Ok(json!(rows))
}
#[tauri::command]
pub fn save_api_key(key: String) -> AppResult<()> {
    let entry = keyring::Entry::new("RL Studio", "openai")
        .map_err(|e| AppError::Internal(e.to_string()))?;
    if key.is_empty() {
        entry
            .delete_credential()
            .map_err(|e| AppError::Internal(e.to_string()))?;
    } else {
        entry
            .set_password(&key)
            .map_err(|e| AppError::Internal(e.to_string()))?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn validates_schema_types_and_bounds() {
        assert!(validate(&json!({"arenas":32,"device":"cpu"})).is_ok());
        assert!(validate(&json!({"arenas":0})).is_err());
        assert!(validate(&json!({"arenas":1.5})).is_err());
        assert!(validate(&json!({"device":"invalid"})).is_err());
        assert!(validate(&json!({"unknown":1})).is_err());
        assert!(validate(&json!([])).is_err());
    }
}
