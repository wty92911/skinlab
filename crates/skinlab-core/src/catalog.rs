// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Release-shipped skin names and numbers. Maps display names to the engine's
//! non-contiguous skin numbers; it says nothing about runtime compatibility.

use serde::{Deserialize, Serialize};
use thiserror::Error;

const EMBEDDED: &str = include_str!("../../../data/catalog.json");
pub const MAX_SKIN_NUMBER: u32 = 100_000;
/// 2: chromas are nested under their parent skin.
pub const SCHEMA_VERSION: u32 = 2;

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
    /// Numeric champion key used by the CN website (full skin id = key * 1000 + skin number).
    #[serde(default)]
    pub key: String,
    pub name: String,
    /// Portrait image reference (see `art`).
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub art: String,
    pub skins: Vec<Skin>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Skin {
    pub skin_id: u32,
    pub name: String,
    /// Card art reference, e.g. `tencent:skinloading/0d4018bf-….jpg` (see `art`).
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub art: String,
    /// Recolors of this skin. Each has its own skin number and goes through the
    /// same update path as the parent.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub chromas: Vec<Chroma>,
    /// `Some(true)` when the skin is known to need form/stack handling beyond
    /// the shared update path. `None` means unknown, not "ordinary".
    #[serde(default)]
    pub requires_special_handling: Option<bool>,
    #[serde(default)]
    pub rarity: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Chroma {
    pub skin_id: u32,
    /// Short name without the parent skin's name, e.g. `玉露金风`.
    pub name: String,
    /// Client swatch colors (`#RRGGBB`); one or two entries, may be empty.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub colors: Vec<String>,
    /// Model render reference (see `art`).
    #[serde(default, skip_serializing_if = "String::is_empty")]
    pub art: String,
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
    #[error("invalid chroma colors for `{0}` skin {1}")]
    ChromaData(String, u32),
    #[error("invalid image reference for `{0}`")]
    Art(String),
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
    pub fn skin(&self, number: u32) -> Option<&Skin> {
        self.skins.iter().find(|s| s.skin_id == number)
    }

    /// The skin that owns `number`, whether it is the skin itself or one of its chromas.
    pub fn family(&self, number: u32) -> Option<(&Skin, Option<&Chroma>)> {
        self.skins.iter().find_map(|s| {
            if s.skin_id == number {
                Some((s, None))
            } else {
                s.chromas.iter().find(|c| c.skin_id == number).map(|c| (s, Some(c)))
            }
        })
    }
}

fn valid_model(model: &str) -> bool {
    let mut chars = model.chars();
    matches!(chars.next(), Some(c) if c.is_ascii_alphabetic())
        && model.len() <= 40
        && chars.all(|c| c.is_ascii_alphanumeric())
}

fn valid_chroma(chroma: &Chroma) -> bool {
    let color = |c: &String| c.len() == 7 && c.starts_with('#') && c[1..].bytes().all(|b| b.is_ascii_hexdigit());
    chroma.colors.len() <= 2 && chroma.colors.iter().all(color)
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
        if self.schema_version != SCHEMA_VERSION || !version_ok {
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
            let images = std::iter::once(&champion.art).chain(
                champion.skins.iter().flat_map(|s| std::iter::once(&s.art).chain(s.chromas.iter().map(|c| &c.art))),
            );
            if images.filter(|a| !a.is_empty()).any(|a| !crate::art::valid(a)) {
                return Err(CatalogError::Art(champion.id.clone()));
            }
            let mut numbers = std::collections::HashSet::new();
            let mut number = |id: u32, name: &str| {
                if id > MAX_SKIN_NUMBER || !numbers.insert(id) {
                    return Err(CatalogError::SkinNumber(champion.id.clone(), id));
                }
                if !(1..=256).contains(&name.chars().count()) {
                    return Err(CatalogError::SkinName(champion.id.clone()));
                }
                Ok(())
            };
            for skin in &champion.skins {
                number(skin.skin_id, &skin.name)?;
                for chroma in &skin.chromas {
                    number(chroma.skin_id, &chroma.name)?;
                    if !valid_chroma(chroma) {
                        return Err(CatalogError::ChromaData(champion.id.clone(), chroma.skin_id));
                    }
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

    pub fn chroma_count(&self) -> usize {
        self.champions.iter().flat_map(|c| &c.skins).map(|s| s.chromas.len()).sum()
    }

    /// Native lookup table consumed by `native/loader/skin_catalog.h`.
    /// `source` must be the exact bytes the catalog was parsed from.
    pub fn native_header(&self, source_sha256: &str) -> String {
        let mut numbers = Vec::new();
        let mut entries = Vec::new();
        for champion in &self.champions {
            let mut skins: Vec<u32> = champion
                .skins
                .iter()
                .flat_map(|s| std::iter::once(s.skin_id).chain(s.chromas.iter().map(|c| c.skin_id)))
                .collect();
            skins.sort_unstable();
            skins.dedup();
            entries.push(format!("    {{\"{}\",{},{}}},", champion.id, numbers.len(), skins.len()));
            numbers.extend(skins);
        }
        let numbers: Vec<String> = numbers.iter().map(u32::to_string).collect();
        let mut out = vec![
            "// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0".to_string(),
            "// Generated from the public catalog. Availability is not a test result.".into(),
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
            r#"{"schema_version":2,"version":"16.21","champions":[{"id":"Smolder","key":"901",
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
            art: String::new(),
            chromas: Vec::new(),
            requires_special_handling: None,
            rarity: r.into(),
        };
        assert_eq!(skin("Ultimate").rarity(), Rarity::Ultimate);
        assert!(skin("Ultimate").needs_special_handling());
        assert_eq!(skin("PrestigeCrystalsmall").rarity(), Rarity::Prestige);
        assert_eq!(skin("Year of the Horse 2026").rarity(), Rarity::Limited);
        assert_eq!(skin("Legend").rarity(), Rarity::Legendary);
    }

    fn chroma(id: u32) -> Chroma {
        Chroma { skin_id: id, name: "金".into(), colors: vec!["#D33528".into()], art: String::new() }
    }

    #[test]
    fn chromas_share_number_space_and_resolve_family() {
        let mut catalog = seed();
        catalog.champions[0].skins[1].chromas.push(chroma(2));
        catalog.validate().unwrap();
        let (skin, found) = catalog.champions[0].family(2).unwrap();
        assert_eq!((skin.skin_id, found.map(|c| c.skin_id)), (1, Some(2)));
        assert!(catalog.native_header("x").contains("sl_catalog_numbers[]={0,1,2};"));

        catalog.champions[0].skins[1].chromas.push(chroma(1));
        assert!(matches!(catalog.validate(), Err(CatalogError::SkinNumber(_, 1))));
    }

    #[test]
    fn chroma_data_is_checked() {
        for bad in [
            Chroma { colors: vec!["red".into()], ..chroma(2) },
            Chroma { colors: vec!["#000000".into(); 3], ..chroma(2) },
        ] {
            let mut catalog = seed();
            catalog.champions[0].skins[1].chromas.push(bad);
            assert!(matches!(catalog.validate(), Err(CatalogError::ChromaData(_, 2))));
        }
    }

    #[test]
    fn images_must_be_known_references() {
        for (art, ok) in [("tencent:skinloading/0d4018bf.jpg", true), ("https://evil.example/x.jpg", false)] {
            let mut catalog = seed();
            catalog.champions[0].skins[1].art = art.into();
            assert_eq!(catalog.validate().is_ok(), ok, "{art}");
            let mut catalog = seed();
            catalog.champions[0].skins[1].chromas.push(Chroma { art: art.into(), ..chroma(2) });
            assert_eq!(catalog.validate().is_ok(), ok, "{art}");
        }
    }

    #[test]
    fn native_header_lists_sorted_numbers() {
        let header = seed().native_header("abc");
        assert!(header.contains("sl_catalog_numbers[]={0,1};"));
        assert!(header.contains("{\"Smolder\",0,2},"));
    }
}
