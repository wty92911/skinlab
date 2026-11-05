// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Game builds whose native adaptation has been verified. A build is added
//! here only after following `docs/ADAPTATION.md`, never by hash alone.

use serde::{Deserialize, Serialize};

const EMBEDDED: &str = include_str!("../../../data/builds.json");

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Build {
    pub version: String,
    pub region: String,
    pub sha256: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SupportedBuilds {
    pub schema_version: u32,
    pub builds: Vec<Build>,
}

/// Offline host diagnostics. Even six unique rules do not verify runtime ABI.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiskBuildCheck {
    pub schema_version: u32,
    pub scope: String,
    pub disk_sha256: String,
    pub required_rules_unique: bool,
    pub runtime_compatible: Option<bool>,
    pub error: String,
    pub results: Vec<DiskRuleCheck>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiskRuleCheck {
    pub name: String,
    pub required: bool,
    pub state: String,
    pub match_rvas: Vec<u32>,
    pub candidate: Option<serde_json::Value>,
}

impl SupportedBuilds {
    pub fn embedded() -> SupportedBuilds {
        let builds: SupportedBuilds = serde_json::from_str(EMBEDDED).expect("data/builds.json is valid");
        assert!(builds.builds.iter().all(|b| is_sha256(&b.sha256)));
        builds
    }

    pub fn find(&self, sha256: &str) -> Option<&Build> {
        self.builds.iter().find(|b| b.sha256 == sha256)
    }

    pub fn latest(&self) -> Option<&Build> {
        self.builds.last()
    }
}

pub fn is_sha256(value: &str) -> bool {
    value.len() == 64 && value.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn embedded_builds_are_lowercase_sha256() {
        let builds = SupportedBuilds::embedded();
        let latest = builds.latest().unwrap();
        assert!(builds.find(&latest.sha256).is_some());
        assert!(builds.find(&latest.sha256.to_uppercase()).is_none());
    }
}
