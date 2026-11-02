// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! The native artifacts built by `cargo xtask native`, verified against their
//! build manifest before every connect.

use serde::Deserialize;
use sha2::{Digest, Sha256};
use std::path::{Path, PathBuf};
use thiserror::Error;

pub const HOST_EXE: &str = "skinlab-observe-host.exe";
pub const MANIFEST: &str = "build-manifest.json";

#[derive(Debug, Error)]
pub enum BundleError {
    #[error("找不到原生组件（{0}），请重新构建或安装")]
    Missing(PathBuf),
    #[error("原生组件清单无效：{0}")]
    Manifest(String),
    #[error("原生组件 {0} 校验失败，请重新构建或安装")]
    Hash(String),
}

#[derive(Debug, Deserialize)]
struct Manifest {
    artifacts: Vec<Artifact>,
}

#[derive(Debug, Deserialize)]
struct Artifact {
    name: String,
    sha256: String,
}

#[derive(Debug, Clone)]
pub struct NativeBundle {
    dir: PathBuf,
}

impl NativeBundle {
    pub fn new(dir: impl Into<PathBuf>) -> Self {
        NativeBundle { dir: dir.into() }
    }

    /// Development layout: `<repo>/out/current.json` names the bundle directory.
    pub fn from_repo(root: &Path) -> Option<Self> {
        #[derive(Deserialize)]
        struct Pointer {
            bundle: String,
        }
        let text = std::fs::read_to_string(root.join("out/current.json")).ok()?;
        let pointer: Pointer = serde_json::from_str(text.trim_start_matches('\u{feff}')).ok()?;
        let valid = pointer.bundle.starts_with("session")
            && pointer.bundle.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit() || b == b'-');
        valid.then(|| NativeBundle::new(root.join("out").join(pointer.bundle)))
    }

    pub fn dir(&self) -> &Path {
        &self.dir
    }

    pub fn host(&self) -> PathBuf {
        self.dir.join(HOST_EXE)
    }

    /// Checks every artifact against the manifest and returns the host path.
    pub fn verify(&self) -> Result<PathBuf, BundleError> {
        let manifest_path = self.dir.join(MANIFEST);
        let text = std::fs::read_to_string(&manifest_path).map_err(|_| BundleError::Missing(manifest_path))?;
        let manifest: Manifest = serde_json::from_str(text.trim_start_matches('\u{feff}'))
            .map_err(|e| BundleError::Manifest(e.to_string()))?;
        if !manifest.artifacts.iter().any(|a| a.name == HOST_EXE) {
            return Err(BundleError::Manifest(format!("缺少 {HOST_EXE}")));
        }
        for artifact in &manifest.artifacts {
            if artifact.name.contains(['/', '\\']) || artifact.name.starts_with('.') {
                return Err(BundleError::Manifest(format!("非法文件名 {}", artifact.name)));
            }
            let path = self.dir.join(&artifact.name);
            let bytes = std::fs::read(&path).map_err(|_| BundleError::Missing(path))?;
            if hex(&Sha256::digest(&bytes)) != artifact.sha256 {
                return Err(BundleError::Hash(artifact.name.clone()));
            }
        }
        Ok(self.host())
    }
}

pub(crate) fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write_bundle(dir: &Path, host: &[u8], recorded: &[u8]) {
        std::fs::write(dir.join(HOST_EXE), host).unwrap();
        let manifest = serde_json::json!({"artifacts":[{"name":HOST_EXE,"sha256":hex(&Sha256::digest(recorded))}]});
        std::fs::write(dir.join(MANIFEST), manifest.to_string()).unwrap();
    }

    #[test]
    fn verifies_matching_artifacts() {
        let dir = tempfile::tempdir().unwrap();
        write_bundle(dir.path(), b"host", b"host");
        assert_eq!(NativeBundle::new(dir.path()).verify().unwrap(), dir.path().join(HOST_EXE));
    }

    #[test]
    fn rejects_modified_artifact() {
        let dir = tempfile::tempdir().unwrap();
        write_bundle(dir.path(), b"tampered", b"host");
        assert!(matches!(NativeBundle::new(dir.path()).verify(), Err(BundleError::Hash(_))));
    }

    #[test]
    fn repo_pointer_rejects_traversal() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::create_dir(dir.path().join("out")).unwrap();
        std::fs::write(dir.path().join("out/current.json"), r#"{"bundle":"session/../../x"}"#).unwrap();
        assert!(NativeBundle::from_repo(dir.path()).is_none());
        std::fs::write(dir.path().join("out/current.json"), "\u{feff}{\"bundle\":\"session-dev1\"}").unwrap();
        assert!(NativeBundle::from_repo(dir.path()).unwrap().dir().ends_with("out/session-dev1"));
    }
}
