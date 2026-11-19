// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Last successful skin choice per champion. Owned by the controller thread.

use std::collections::BTreeMap;
use std::io;
use std::path::{Path, PathBuf};

pub(crate) struct SkinMemory {
    path: PathBuf,
    skins: BTreeMap<String, u32>,
}

impl SkinMemory {
    pub(crate) fn load(path: &Path) -> Self {
        let skins = match std::fs::read(path) {
            Ok(bytes) => serde_json::from_slice(&bytes).unwrap_or_else(|error| {
                tracing::warn!(%error, "saved skin choices could not be read");
                BTreeMap::new()
            }),
            Err(error) => {
                if error.kind() != io::ErrorKind::NotFound {
                    tracing::warn!(%error, "saved skin choices could not be opened");
                }
                BTreeMap::new()
            }
        };
        Self { path: path.into(), skins }
    }

    pub(crate) fn get(&self, champion: &str) -> Option<u32> {
        self.skins.get(champion).copied()
    }

    pub(crate) fn set(&mut self, champion: &str, skin: Option<u32>) -> io::Result<()> {
        if self.get(champion) == skin {
            return Ok(());
        }
        let mut skins = self.skins.clone();
        if let Some(skin) = skin {
            skins.insert(champion.into(), skin);
        } else {
            skins.remove(champion);
        }
        if let Some(parent) = self.path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        let temporary = self.path.with_extension("json.tmp");
        std::fs::write(&temporary, serde_json::to_vec_pretty(&skins)?)?;
        std::fs::rename(temporary, &self.path)?;
        self.skins = skins;
        Ok(())
    }
}
