// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Native build: the same flags `scripts/build-session.ps1` used, plus a
//! manifest the app verifies before every connect.

use anyhow::{Context, Result, bail};
use sha2::{Digest, Sha256};
use skinlab_core::Catalog;
use std::path::{Path, PathBuf};
use std::process::Command;

const COMMON: &[&str] = &[
    "-std=c++17",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-static",
    "-D_WIN32_WINNT=0x0601",
    "-DSKINLAB_EMBEDDED_CORE",
    "-Inative/include",
    "-Inative/loader",
];
const SCAN: &[&str] = &["native/src/profile_scan.cpp", "native/src/profile_rules_generated.cpp"];
const ARTIFACTS: &[&str] = &["skinlab-observe.dll", "skinlab-observe-fixture.exe", "skinlab-observe-host.exe"];
const GENERATED_HEADER: &str = "native/loader/skin_catalog_generated.h";

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

fn sha256(path: &Path) -> Result<String> {
    Ok(hex(&Sha256::digest(std::fs::read(path).with_context(|| path.display().to_string())?)))
}

pub fn catalog_header(root: &Path) -> Result<PathBuf> {
    let source = std::fs::read_to_string(root.join("data/catalog.json"))?;
    let catalog = Catalog::parse(&source)?;
    let header = catalog.native_header(&hex(&Sha256::digest(source.as_bytes())));
    let path = root.join(GENERATED_HEADER);
    std::fs::write(&path, header)?;
    println!("{} champions, {} skins -> {GENERATED_HEADER}", catalog.champions.len(), catalog.skin_count());
    Ok(path)
}

fn find_compiler(explicit: Option<&str>) -> Result<String> {
    let candidates: Vec<String> = match explicit {
        Some(path) => vec![path.into()],
        None => ["x86_64-w64-mingw32-g++", "g++"].map(String::from).to_vec(),
    };
    for candidate in candidates {
        let Ok(output) = Command::new(&candidate).arg("-dumpmachine").output() else { continue };
        let machine = String::from_utf8_lossy(&output.stdout);
        if machine.starts_with("x86_64-") && machine.contains("mingw") {
            return Ok(candidate);
        }
        if explicit.is_some() {
            bail!("{candidate} targets {}, need x86_64 MinGW-w64", machine.trim());
        }
    }
    bail!("install an x64 MinGW-w64 g++ or pass --compiler <path>")
}

fn compile(root: &Path, compiler: &str, args: &[&str], what: &str) -> Result<()> {
    println!("building {what}");
    let status = Command::new(compiler).current_dir(root).args(COMMON).args(args).status()?;
    if !status.success() {
        bail!("{what} build failed");
    }
    Ok(())
}

pub fn build(root: &Path, args: &[String]) -> Result<()> {
    let bundle = crate::flag(args, "--bundle").unwrap_or("session");
    let valid = bundle == "session"
        || bundle.strip_prefix("session-").is_some_and(|rest| {
            !rest.is_empty() && rest.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-')
        });
    if !valid {
        bail!("--bundle must look like session or session-<name>");
    }
    let compiler = find_compiler(crate::flag(args, "--compiler"))?;
    let out = format!("out/{bundle}");
    std::fs::create_dir_all(root.join(&out))?;
    catalog_header(root)?;

    let dll = format!("{out}/skinlab-observe.dll");
    let mut dll_args = vec!["-shared"];
    if args.iter().any(|arg| arg == "--startup-trace") {
        dll_args.push("-DSKINLAB_STARTUP_TRACE");
    }
    dll_args.extend(SCAN);
    dll_args.extend([
        "native/src/profile_snapshot_win.cpp",
        "native/src/core.cpp",
        "native/src/dx11_bridge.cpp",
        "native/loader/observe_hook.cpp",
        "-o",
        &dll,
        "-Wl,--exclude-all-symbols",
        "-luser32",
        "-lbcrypt",
    ]);
    compile(root, &compiler, &dll_args, "session DLL")?;

    let fixture = format!("{out}/skinlab-observe-fixture.exe");
    compile(
        root,
        &compiler,
        &["-municode", "native/lab/observe_fixture.cpp", "-o", &fixture, "-luser32", "-ld3d11", "-ldxgi", "-ldxguid"],
        "WARP fixture",
    )?;

    let host = format!("{out}/skinlab-observe-host.exe");
    let mut host_args = vec!["-municode"];
    host_args.extend(SCAN);
    host_args.extend(["native/loader/observe_host.cpp", "-o", &host, "-luser32", "-lbcrypt", "-ladvapi32"]);
    compile(root, &compiler, &host_args, "session host")?;

    let mut sources = Vec::new();
    for dir in ["native/include/skinlab", "native/src", "native/loader", "native/lab"] {
        let mut files: Vec<PathBuf> =
            std::fs::read_dir(root.join(dir))?.filter_map(|e| e.ok().map(|e| e.path())).collect();
        files.sort();
        for file in files {
            let relative = file.strip_prefix(root)?.to_string_lossy().replace('\\', "/");
            sources.push(serde_json::json!({"path": relative, "sha256": sha256(&file)?}));
        }
    }
    let artifacts = ARTIFACTS
        .iter()
        .map(|name| {
            let path = root.join(&out).join(name);
            Ok(serde_json::json!({"name": name, "bytes": std::fs::metadata(&path)?.len(), "sha256": sha256(&path)?}))
        })
        .collect::<Result<Vec<_>>>()?;
    let manifest = serde_json::json!({
        "compiler": compiler,
        "target": "x86_64-w64-mingw32",
        "bundle": bundle,
        "startup_trace": args.iter().any(|arg| arg == "--startup-trace"),
        "sources": sources,
        "artifacts": artifacts,
    });
    std::fs::write(root.join(&out).join("build-manifest.json"), serde_json::to_string_pretty(&manifest)?)?;
    std::fs::write(
        root.join("out/current.json"),
        serde_json::to_string_pretty(&serde_json::json!({"bundle": bundle}))?,
    )?;
    println!("native bundle ready: {out}");
    Ok(())
}
