//! Release-shipped skin names and numbers. Maps display names to the engine's
//! non-contiguous skin numbers; it says nothing about runtime compatibility.

use serde::{Deserialize, Serialize};
use thiserror::Error;

const EMBEDDED: &str = include_str!("../../../data/catalog.json");
pub const MAX_SKIN_NUMBER: u32 = 100_000;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Catalog {
    pub schema_version: u32,
    pub version: String,
    #[serde(default)]
    pub locale: String,
    #[serde(default)]
    pub source: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub source_file_time: Option<String>,
    #[serde(default)]
    pub updated_at_utc: String,
    pub champions: Vec<Champion>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Champion {
    /// Engine model name, e.g. `Lux`. Matches the Live Client `rawChampionName` suffix.
    pub id: String,
    /// Numeric champion key used by the CN website (skin art = key * 1000 + skin number).
    #[serde(default)]
    pub key: String,
    pub name: String,
    pub skins: Vec<Skin>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Skin {
    pub skin_id: u32,
    pub name: String,
    #[serde(default)]
    pub has_chroma_variants: bool,
    /// `Some(true)` when the skin is known to need form/stack handling beyond
    /// the shared update path. `None` means unknown, not "ordinary".
    #[serde(default)]
    pub requires_special_handling: Option<bool>,
    #[serde(default)]
    pub rarity: String,
}

#[derive(Debug, Error, PartialEq, Eq)]
pub enum CatalogError {
    #[error("catalog JSON is invalid: {0}")]
    Json(String),
    #[error("invalid catalog version/schema")]
    Schema,
    #[error("invalid champion count")]
    ChampionCount,
    #[error("invalid or duplicate champion ID `{0}`")]
    ChampionId(String),
    #[error("invalid or duplicate skin number {1} for `{0}`")]
    SkinNumber(String, u32),
    #[error("invalid skin name for `{0}`")]
    SkinName(String),
    #[error("champion `{0}` lacks the default skin")]
    MissingDefault(String),
}

/// UI grouping for the CN website's free-form `emblemsName` values.
#[derive(Debug, Clone, Copy, Serialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Rarity {
    Base,
    Common,
    Epic,
    Legendary,
    Mythic,
    Ultimate,
    Prestige,
    Limited,
}

impl Rarity {
    pub fn label(self) -> &'static str {
        match self {
            Rarity::Base => "经典",
            Rarity::Common => "普通",
            Rarity::Epic => "史诗",
            Rarity::Legendary => "传说",
            Rarity::Mythic => "神话",
            Rarity::Ultimate => "终极",
            Rarity::Prestige => "至臻",
            Rarity::Limited => "限定",
        }
    }
}

impl Skin {
    pub fn rarity(&self) -> Rarity {
        let raw = self.rarity.to_ascii_lowercase();
        match raw.as_str() {
            "base" => Rarity::Base,
            "" | "common" => Rarity::Common,
            "epic" => Rarity::Epic,
            "legend" | "legendary" => Rarity::Legendary,
            "mythic" => Rarity::Mythic,
            "ultimate" => Rarity::Ultimate,
            _ if raw.starts_with("prestige") => Rarity::Prestige,
            _ => Rarity::Limited,
        }
    }

    pub fn needs_special_handling(&self) -> bool {
        self.requires_special_handling == Some(true) || self.rarity() == Rarity::Ultimate
    }
}

impl Champion {
    /// Full skin identifier used by the CN website's art paths.
    pub fn art_id(&self, skin: u32) -> Option<u64> {
        let key: u64 = self.key.parse().ok()?;
        Some(key * 1000 + u64::from(skin))
    }

    pub fn skin(&self, number: u32) -> Option<&Skin> {
        self.skins.iter().find(|s| s.skin_id == number)
    }
}

fn valid_model(model: &str) -> bool {
    let mut chars = model.chars();
    matches!(chars.next(), Some(c) if c.is_ascii_alphabetic())
        && model.len() <= 40
        && chars.all(|c| c.is_ascii_alphanumeric())
}

impl Catalog {
    /// The catalog compiled into this binary from `data/catalog.json`.
    pub fn embedded() -> Catalog {
        Catalog::parse(EMBEDDED).expect("embedded catalog is validated at build time by tests")
    }

    pub fn parse(json: &str) -> Result<Catalog, CatalogError> {
        let catalog: Catalog = serde_json::from_str(json).map_err(|e| CatalogError::Json(e.to_string()))?;
        catalog.validate()?;
        Ok(catalog)
    }

    pub fn validate(&self) -> Result<(), CatalogError> {
        let version_ok = {
            let parts: Vec<&str> = self.version.split('.').collect();
            (2..=3).contains(&parts.len())
                && parts.iter().all(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()))
        };
        if self.schema_version != 1 || !version_ok {
            return Err(CatalogError::Schema);
        }
        if !(1..=500).contains(&self.champions.len()) {
            return Err(CatalogError::ChampionCount);
        }
        let mut models = std::collections::HashSet::new();
        for champion in &self.champions {
            if !valid_model(&champion.id) || !models.insert(champion.id.as_str()) {
                return Err(CatalogError::ChampionId(champion.id.clone()));
            }
            let mut numbers = std::collections::HashSet::new();
            for skin in &champion.skins {
                if skin.skin_id > MAX_SKIN_NUMBER || !numbers.insert(skin.skin_id) {
                    return Err(CatalogError::SkinNumber(champion.id.clone(), skin.skin_id));
                }
                if !(1..=256).contains(&skin.name.chars().count()) {
                    return Err(CatalogError::SkinName(champion.id.clone()));
                }
            }
            if !numbers.contains(&0) {
                return Err(CatalogError::MissingDefault(champion.id.clone()));
            }
        }
        Ok(())
    }

    pub fn champion(&self, model: &str) -> Option<&Champion> {
        self.champions.iter().find(|c| c.id.eq_ignore_ascii_case(model))
    }

    pub fn skin_count(&self) -> usize {
        self.champions.iter().map(|c| c.skins.len()).sum()
    }

    /// Native lookup table consumed by `native/loader/skin_catalog.h`.
    /// `source` must be the exact bytes the catalog was parsed from.
    pub fn native_header(&self, source_sha256: &str) -> String {
        let mut numbers = Vec::new();
        let mut entries = Vec::new();
        for champion in &self.champions {
            let mut skins: Vec<u32> = champion.skins.iter().map(|s| s.skin_id).collect();
            skins.sort_unstable();
            skins.dedup();
            entries.push(format!("    {{\"{}\",{},{}}},", champion.id, numbers.len(), skins.len()));
            numbers.extend(skins);
        }
        let numbers: Vec<String> = numbers.iter().map(u32::to_string).collect();
        let mut out = vec![
            "// Generated from the public catalog. Availability is not a test result.".to_string(),
            format!("// Source SHA256: {source_sha256}"),
            "#ifndef SKINLAB_CATALOG_GENERATED_H".into(),
            "#define SKINLAB_CATALOG_GENERATED_H".into(),
            format!("static const int32_t sl_catalog_numbers[]={{{}}};", numbers.join(",")),
            "struct SlCatalogModel {const char* name;uint32_t first,count;};".into(),
            "static const SlCatalogModel sl_catalog_models[]={".into(),
        ];
        out.extend(entries);
        out.extend(["};".into(), "#endif".into(), String::new()]);
        out.join("\n")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn seed() -> Catalog {
        serde_json::from_str(
            r#"{"schema_version":1,"version":"16.21","champions":[{"id":"Smolder","key":"901",
            "name":"斯莫德","skins":[{"skin_id":0,"name":"原皮"},{"skin_id":1,"name":"天龙之子"}]}]}"#,
        )
        .unwrap()
    }

    #[test]
    fn shipped_catalog_is_valid() {
        let catalog = Catalog::embedded();
        assert!(catalog.champions.len() > 100);
        assert!(catalog.champion("lux").is_some());
    }

    #[test]
    fn duplicate_skin_is_rejected() {
        let mut catalog = seed();
        let first = catalog.champions[0].skins[0].clone();
        catalog.champions[0].skins.push(first);
        assert!(matches!(catalog.validate(), Err(CatalogError::SkinNumber(_, 0))));
    }

    #[test]
    fn path_like_champion_is_rejected() {
        let mut catalog = seed();
        catalog.champions[0].id = "../bad".into();
        assert!(matches!(catalog.validate(), Err(CatalogError::ChampionId(_))));
    }

    #[test]
    fn missing_default_is_rejected() {
        let mut catalog = seed();
        catalog.champions[0].skins.remove(0);
        assert!(matches!(catalog.validate(), Err(CatalogError::MissingDefault(_))));
    }

    #[test]
    fn rarity_groups_free_form_names() {
        let skin = |r: &str| Skin {
            skin_id: 1,
            name: "x".into(),
            has_chroma_variants: false,
            requires_special_handling: None,
            rarity: r.into(),
        };
        assert_eq!(skin("Ultimate").rarity(), Rarity::Ultimate);
        assert!(skin("Ultimate").needs_special_handling());
        assert_eq!(skin("PrestigeCrystalsmall").rarity(), Rarity::Prestige);
        assert_eq!(skin("Year of the Horse 2026").rarity(), Rarity::Limited);
        assert_eq!(skin("Legend").rarity(), Rarity::Legendary);
    }

    #[test]
    fn art_id_uses_champion_key() {
        assert_eq!(seed().champions[0].art_id(1), Some(901_001));
    }

    #[test]
    fn native_header_lists_sorted_numbers() {
        let header = seed().native_header("abc");
        assert!(header.contains("sl_catalog_numbers[]={0,1};"));
        assert!(header.contains("{\"Smolder\",0,2},"));
    }
}
