fn main() {
    // Generates the permission/capability schemas under `gen/`, embeds the Windows
    // resource (icon, manifest) and re-runs whenever tauri.conf.json changes.
    tauri_build::build()
}
