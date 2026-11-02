// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Versioning and release notes. The workspace version in the root Cargo.toml
//! is the single source; Tauri reads it for the installer.

use anyhow::{Context, Result, bail, ensure};
use skinlab_core::{Catalog, SupportedBuilds};
use std::path::Path;
use std::process::Command;

fn git(root: &Path, args: &[&str]) -> Result<String> {
    let output = Command::new("git").current_dir(root).args(args).output()?;
    ensure!(
        output.status.success(),
        "git {} failed: {}",
        args.join(" "),
        String::from_utf8_lossy(&output.stderr).trim()
    );
    Ok(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

fn is_version(text: &str) -> bool {
    let (core, pre) = text.split_once('-').map_or((text, None), |(c, p)| (c, Some(p)));
    let parts: Vec<&str> = core.split('.').collect();
    parts.len() == 3
        && parts.iter().all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()))
        && pre.is_none_or(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'.'))
}

/// The `version = "x.y.z"` line inside `[workspace.package]`.
fn workspace_version(manifest: &str) -> Option<&str> {
    let section = manifest.split("[workspace.package]").nth(1)?;
    let line = section.lines().take_while(|l| !l.starts_with('[')).find(|l| l.starts_with("version"))?;
    line.split('"').nth(1)
}

pub fn current_version(root: &Path) -> Result<String> {
    let manifest = std::fs::read_to_string(root.join("Cargo.toml"))?;
    Ok(workspace_version(&manifest).context("no version in [workspace.package]")?.to_string())
}

/// `cargo xtask release 0.5.0 [--push]`: bump, commit and tag `v0.5.0`.
pub fn release(root: &Path, args: &[String]) -> Result<()> {
    let version = args.first().context("usage: cargo xtask release <x.y.z> [--push]")?;
    ensure!(is_version(version), "{version} is not x.y.z or x.y.z-pre");
    let tag = format!("v{version}");
    ensure!(git(root, &["status", "--porcelain"])?.is_empty(), "commit or stash changes first");
    ensure!(git(root, &["tag", "--list", &tag])?.is_empty(), "tag {tag} already exists");

    let old = current_version(root)?;
    if &old != version {
        let path = root.join("Cargo.toml");
        let manifest = std::fs::read_to_string(&path)?;
        let from = format!("[workspace.package]\nversion = \"{old}\"");
        ensure!(manifest.contains(&from), "unexpected [workspace.package] layout");
        std::fs::write(&path, manifest.replace(&from, &format!("[workspace.package]\nversion = \"{version}\"")))?;
        let readme = root.join("README.md");
        let text = std::fs::read_to_string(&readme)?;
        std::fs::write(&readme, text.replace(&format!("当前版本 {old}"), &format!("当前版本 {version}")))?;
        let status = Command::new("cargo").current_dir(root).args(["update", "--workspace", "--quiet"]).status()?;
        ensure!(status.success(), "cargo update --workspace failed");
        git(root, &["add", "Cargo.toml", "Cargo.lock", "README.md"])?;
        git(root, &["commit", "--quiet", "-m", &format!("Release {tag}")])?;
    }
    git(root, &["tag", "-a", &tag, "-m", &format!("SkinLab {tag}")])?;
    println!("tagged {tag} ({old} -> {version})");
    if args.iter().any(|a| a == "--push") {
        git(root, &["push", "--quiet", "origin", "HEAD", &tag])?;
        println!("pushed; CI builds and publishes the release");
    } else {
        println!("publish with: git push origin HEAD {tag}");
    }
    Ok(())
}

/// Markdown body for the GitHub release of `tag`; fails if the tag and the
/// workspace version disagree, so a mistagged commit never publishes.
pub fn notes(root: &Path, args: &[String]) -> Result<()> {
    let tag = args.first().context("usage: cargo xtask release-notes <vX.Y.Z>")?;
    let version = current_version(root)?;
    if tag.strip_prefix('v') != Some(version.as_str()) {
        bail!("tag {tag} does not match workspace version {version}");
    }
    let previous = git(root, &["describe", "--tags", "--abbrev=0", &format!("{tag}^")]).ok();
    let range = previous.as_ref().map_or(tag.clone(), |p| format!("{p}..{tag}"));
    let log = git(root, &["log", "--no-merges", "--invert-grep", "--grep=^Release v", "--format=- %s (%h)", &range])?;

    let builds = SupportedBuilds::embedded();
    let catalog = Catalog::embedded();
    let mut out = String::new();
    out.push_str("## 适配\n\n");
    out.push_str(&format!(
        "- 游戏版本：{}\n",
        builds.builds.iter().map(|b| format!("`{}`（{}）", b.version, b.region)).collect::<Vec<_>>().join("、")
    ));
    out.push_str(&format!(
        "- 皮肤目录：{} · {} 位英雄 · {} 款\n\n",
        catalog.version,
        catalog.champions.len(),
        catalog.skin_count()
    ));
    out.push_str("游戏更新到未适配的版本时，SkinLab 会显示“游戏版本尚未适配”且不会接入。\n\n");
    out.push_str(&format!("## 变更{}\n\n", previous.as_ref().map(|p| format!("（自 {p}）")).unwrap_or_default()));
    out.push_str(if log.is_empty() { "- 无代码变更" } else { &log });
    out.push_str("\n\n## 安装\n\n");
    out.push_str(
        "下载 `SkinLab_*_x64-setup.exe` 安装，启动时需要管理员权限。安装包未做代码签名，Windows 可能提示“未知发布者”。",
    );
    out.push_str("可用 `SHA256SUMS.txt` 校验下载文件。\n\n");
    out.push_str("> 修改游戏内存违反游戏用户协议，存在账号处罚风险，SkinLab 不提供任何免封保证。\n");
    print!("{out}");
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_shapes() {
        assert!(is_version("0.5.0"));
        assert!(is_version("1.0.0-rc.1"));
        assert!(!is_version("0.5"));
        assert!(!is_version("v0.5.0"));
        assert!(!is_version("0.5.0-"));
    }

    #[test]
    fn reads_workspace_package_version_only() {
        let manifest = "[package]\nversion = \"9.9.9\"\n[workspace.package]\nversion = \"0.4.0\"\nedition = \"2024\"\n";
        assert_eq!(workspace_version(manifest), Some("0.4.0"));
    }
}
