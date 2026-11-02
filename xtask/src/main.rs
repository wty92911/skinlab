//! `cargo xtask <task>` — maintainer tasks. Run from anywhere in the repository.

mod catalog;
mod native;
mod verify;

use anyhow::{Context, Result, bail};
use std::path::{Path, PathBuf};

const USAGE: &str = "\
Usage: cargo xtask <task>

  native [--compiler PATH] [--bundle session[-name]]
        Build the native host, DLL and WARP fixture into out/<bundle>/ (Windows x64
        MinGW-w64 g++, or x86_64-w64-mingw32-g++ for cross builds).
  catalog-header
        Regenerate native/loader/skin_catalog_generated.h from data/catalog.json.
  update-catalog [--output PATH]
        Refresh data/catalog.json from the CN website's public data.
  build-id <League of Legends.exe>
        Print the version and SHA256 entry for data/builds.json. Add it only after
        completing docs/ADAPTATION.md.
  verify
        Windows: run the host self-test and the controller against the WARP fixture.
";

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let root = repo_root()?;
    match args.first().map(String::as_str) {
        Some("native") => native::build(&root, &args[1..]),
        Some("catalog-header") => native::catalog_header(&root).map(drop),
        Some("update-catalog") => catalog::update(&root, &args[1..]),
        Some("build-id") => match args.get(1) {
            Some(exe) => build_id(Path::new(exe)),
            None => bail!("build-id needs the game executable path"),
        },
        Some("verify") => verify::run(&root),
        _ => {
            print!("{USAGE}");
            Ok(())
        }
    }
}

fn repo_root() -> Result<PathBuf> {
    let manifest = Path::new(env!("CARGO_MANIFEST_DIR"));
    Ok(manifest.parent().context("xtask lives in the repository root")?.to_path_buf())
}

pub(crate) fn flag<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).map(String::as_str)
}

fn build_id(exe: &Path) -> Result<()> {
    let sha256 = skinlab_session::game::sha256_file(exe).with_context(|| format!("reading {}", exe.display()))?;
    let version = skinlab_session::game::file_version(exe).unwrap_or_else(|| "<fill in>".into());
    let known = skinlab_core::SupportedBuilds::embedded().find(&sha256).cloned();
    let entry = serde_json::json!({"version": version, "region": "cn", "sha256": sha256});
    println!("{}", serde_json::to_string_pretty(&entry)?);
    match known {
        Some(build) => println!("already supported as {}", build.version),
        None => println!("not in data/builds.json — follow docs/ADAPTATION.md before adding it"),
    }
    Ok(())
}
