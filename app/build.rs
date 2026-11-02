fn main() {
    // The native host must run at or above the game's integrity level, so the
    // whole app asks for elevation once at launch instead of re-launching itself.
    let windows = tauri_build::WindowsAttributes::new().app_manifest(include_str!("skinlab.exe.manifest"));
    tauri_build::try_build(tauri_build::Attributes::new().windows_attributes(windows))
        .expect("failed to run tauri-build");
}
