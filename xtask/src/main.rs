// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! `cargo xtask <task>` — maintainer tasks. Run from anywhere in the repository.

mod catalog;
mod native;
mod release;
mod verify;

use anyhow::{Context, Result, bail};
use std::path::{Path, PathBuf};

const USAGE: &str = "\
Usage: cargo xtask <task>

  native [--compiler PATH] [--bundle session[-name]] [--startup-trace]
        Build the native host, DLL and WARP fixture into out/<bundle>/ (Windows x64
        MinGW-w64 g++, or x86_64-w64-mingw32-g++ for cross builds).
  catalog-header
        Regenerate native/loader/skin_catalog_generated.h from data/catalog.json.
  update-catalog [--output PATH]
        Refresh data/catalog.json from the CN website's public data.
  build-id <League of Legends.exe>
        Print the version and SHA256 entry for data/builds.json. Add it only after
        completing docs/ADAPTATION.md.
  check-build <League of Legends.exe>
        Scan disk PE features without launching or attaching to the game. This
        diagnostic never grants runtime compatibility or changes data/builds.json.
  verify
        Windows: run the host self-test and the controller against the WARP fixture.
  release <x.y.z> [--push]
        Set the workspace version, commit \"Release vX.Y.Z\", and tag it. Pushing the
        tag makes CI build, verify and publish the GitHub release.
  release-notes <vX.Y.Z>
        Print the release body (used by CI); fails if the tag and version disagree.
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
        Some("check-build") => {
            let exe = args.get(1).context("check-build needs the game executable path")?;
            let bundle = skinlab_session::NativeBundle::from_repo(&root).context("run cargo xtask native first")?;
            let check = skinlab_session::game::check_build(&bundle.verify()?, Path::new(exe))?;
            println!("{}", serde_json::to_string_pretty(&check)?);
            println!("{}", skinlab_core::error::build_check_message(Some(&check)));
            if !check.error.is_empty() {
                bail!("disk image scan failed");
            }
            Ok(())
        }
        Some("verify") => verify::run(&root),
        Some("release") => release::release(&root, &args[1..]),
        Some("release-notes") => release::notes(&root, &args[1..]),
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
