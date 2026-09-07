/**
 * Shared types for the Tauri IPC boundary.
 *
 * Every shape here has an exact counterpart in `src-tauri/src/lib.rs`. Rust structs are
 * annotated `#[serde(rename_all = "camelCase")]` so the wire format is idiomatic
 * JavaScript on this side and idiomatic Rust on the other; keep the two in step.
 */

/** Result of the `app_info` command. Identity of the running build. */
export interface AppInfo {
  /** Product name as configured in tauri.conf.json, e.g. "RL Studio". */
  readonly name: string;
  /** Semver version, sourced from app/package.json at build time. */
  readonly version: string;
  /** Version of the Tauri runtime this binary was compiled against. */
  readonly tauriVersion: string;
}

/**
 * The serialized form of `AppError` from the Rust side.
 *
 * Tauri rejects a command by serializing its error value, so every failure crosses the
 * bridge as this object rather than as a bare string. `kind` is a stable machine-readable
 * discriminant the UI can branch on; `message` is for humans.
 */
export interface AppErrorPayload {
  readonly kind: AppErrorKind;
  readonly message: string;
}

export type AppErrorKind = "engine" | "io" | "config" | "internal";

/** Narrows an unknown `invoke` rejection to a structured error, when it is one. */
export function isAppError(value: unknown): value is AppErrorPayload {
  return (
    typeof value === "object" &&
    value !== null &&
    "kind" in value &&
    "message" in value &&
    typeof (value as { message: unknown }).message === "string"
  );
}
