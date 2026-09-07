//! The single error type crossing the Tauri IPC boundary.
//!
//! Tauri rejects a command by *serializing* its error value. If that value were a bare
//! `String` the frontend would have to pattern-match on English prose to decide whether
//! a failure is retryable, so every error carries a stable machine-readable `kind`
//! alongside its human message.

use serde::{Serialize, Serializer};

/// Every way an RL Studio command can fail.
///
/// Variants are deliberately coarse. The frontend branches on the category (can I retry?
/// is this the user's fault?); the message carries the detail.
#[derive(Debug, thiserror::Error)]
pub enum AppError {
    /// The rl-engine child process or its WebSocket misbehaved.
    #[error("{0}")]
    Engine(String),

    /// Filesystem trouble: a run directory, config file or checkpoint.
    #[error("{0}")]
    Io(String),

    /// A configuration value failed validation against the JSON Schema.
    #[error("{0}")]
    Config(String),

    /// A bug on our side. The user cannot act on it; we log it and say sorry.
    #[error("{0}")]
    Internal(String),
}

impl AppError {
    /// The discriminant the frontend switches on. Kept in sync with `AppErrorKind`
    /// in `app/src/lib/types.ts`.
    fn kind(&self) -> &'static str {
        match self {
            AppError::Engine(_) => "engine",
            AppError::Io(_) => "io",
            AppError::Config(_) => "config",
            AppError::Internal(_) => "internal",
        }
    }
}

// Hand-written rather than `#[derive(Serialize)]`: derive would emit serde's externally
// tagged enum shape (`{"Engine": "..."}`), which is awkward to consume and would change
// whenever a variant is renamed. This shape is a contract.
impl Serialize for AppError {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        use serde::ser::SerializeStruct;
        let mut state = serializer.serialize_struct("AppError", 2)?;
        state.serialize_field("kind", self.kind())?;
        state.serialize_field("message", &self.to_string())?;
        state.end()
    }
}

impl From<std::io::Error> for AppError {
    fn from(value: std::io::Error) -> Self {
        AppError::Io(value.to_string())
    }
}

impl From<serde_json::Error> for AppError {
    fn from(value: serde_json::Error) -> Self {
        AppError::Config(value.to_string())
    }
}

impl From<tauri::Error> for AppError {
    fn from(value: tauri::Error) -> Self {
        AppError::Internal(value.to_string())
    }
}

/// Return type of every `#[tauri::command]`. Commands never panic and never `unwrap`;
/// they return this and let the frontend decide what to show.
pub type AppResult<T> = Result<T, AppError>;
