// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
fn main() {
    // Build provenance shown in the About panel; CI checkouts provide GITHUB_SHA.
    let commit = std::env::var("GITHUB_SHA").ok().or_else(|| {
        let out = std::process::Command::new("git").args(["rev-parse", "HEAD"]).output().ok()?;
        out.status.success().then(|| String::from_utf8_lossy(&out.stdout).trim().to_string())
    });
    println!("cargo:rustc-env=SKINLAB_COMMIT={}", commit.as_deref().map_or("unknown", |c| &c[..c.len().min(12)]));
    println!("cargo:rerun-if-env-changed=GITHUB_SHA");
    println!("cargo:rerun-if-changed=../.git/HEAD");
    println!("cargo:rerun-if-changed=../.git/refs/heads");

    // The native host must run at or above the game's integrity level, so the
    // whole app asks for elevation once at launch instead of re-launching itself.
    let windows = tauri_build::WindowsAttributes::new().app_manifest(include_str!("skinlab.exe.manifest"));
    tauri_build::try_build(tauri_build::Attributes::new().windows_attributes(windows))
        .expect("failed to run tauri-build");
}
