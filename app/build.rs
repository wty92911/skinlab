// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
fn main() {
    let development = std::env::var("PROFILE").as_deref() == Ok("debug");
    // Build provenance shown in the About panel; CI checkouts provide GITHUB_SHA.
    let commit = std::env::var("GITHUB_SHA").ok().or_else(|| {
        let out = std::process::Command::new("git").args(["rev-parse", "HEAD"]).output().ok()?;
        out.status.success().then(|| String::from_utf8_lossy(&out.stdout).trim().to_string())
    });
    println!("cargo:rustc-env=SKINLAB_COMMIT={}", commit.as_deref().map_or("unknown", |c| &c[..c.len().min(12)]));
    println!("cargo:rerun-if-env-changed=GITHUB_SHA");
    println!("cargo:rerun-if-changed=../.git/HEAD");
    println!("cargo:rerun-if-changed=../.git/refs/heads");

    // Development loads out/current.json at runtime. Copying the release-only
    // out/session resources here would require (or silently reuse) an old DLL.
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") && development {
        let mut config: serde_json::Value = std::env::var("TAURI_CONFIG")
            .map(|text| serde_json::from_str(&text).expect("valid TAURI_CONFIG"))
            .unwrap_or_else(|_| serde_json::json!({}));
        config["bundle"]["resources"] = serde_json::json!([]);
        let config = config.to_string();
        // SAFETY: this build script is single-threaded; no environment readers
        // run concurrently. Set this before tauri-build reads the configuration.
        unsafe { std::env::set_var("TAURI_CONFIG", &config) };
        println!("cargo:rustc-env=TAURI_CONFIG={config}");
    }

    // Release apps request elevation once at launch. Development and test
    // executables inherit the terminal's rights so cargo test works without UAC.
    let manifest = include_str!("skinlab.exe.manifest");
    let manifest = if development {
        manifest.replace("level=\"requireAdministrator\"", "level=\"asInvoker\"")
    } else {
        manifest.to_string()
    };
    let windows = tauri_build::WindowsAttributes::new().app_manifest(manifest);
    tauri_build::try_build(tauri_build::Attributes::new().windows_attributes(windows))
        .expect("failed to run tauri-build");
}
