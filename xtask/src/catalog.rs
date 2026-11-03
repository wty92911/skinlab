// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Catalog refresh from the CN website's public champion data. The result is
//! reviewed and shipped with a release; the app never fetches it at runtime.
//!
//! Images are recorded as references (`skinlab_core::art`) to the CN website's
//! CDN and loaded on demand; the app hides any that turn out to be missing.

use anyhow::{Context, Result, bail};
use serde::Deserialize;
use skinlab_core::catalog::SCHEMA_VERSION;
use skinlab_core::{Catalog, Champion, Chroma, Skin, art};
use std::collections::HashMap;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::mpsc::channel;
use std::time::Duration;

const TENCENT: &str = "https://game.gtimg.cn/images/lol/act/img/js/";
/// Community mirror of the client's game data, for chroma swatch colors.
const CDRAGON: &str =
    "https://raw.communitydragon.org/latest/plugins/rcp-be-lol-game-data/global/default/v1/champions/";
const MAX_BODY: u64 = 8 * 1024 * 1024;
const WORKERS: usize = 6;

#[derive(Debug, Deserialize)]
struct HeroList {
    hero: Vec<HeroItem>,
    version: String,
    #[serde(rename = "fileTime", default)]
    file_time: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
struct HeroItem {
    #[serde(rename = "heroId")]
    hero_id: String,
    name: String,
    alias: String,
    #[serde(default)]
    title: Option<String>,
}

#[derive(Debug, Deserialize)]
struct HeroDetail {
    skins: Vec<RawSkin>,
}

#[derive(Debug, Deserialize)]
struct RawSkin {
    #[serde(rename = "skinId")]
    skin_id: String,
    name: String,
    #[serde(rename = "chromasBelongId", default)]
    chromas_belong_id: Option<String>,
    #[serde(rename = "emblemsName", default)]
    emblems_name: Option<String>,
    #[serde(rename = "loadingImg", default)]
    loading_img: Option<String>,
}

#[derive(Debug, Default, Deserialize)]
struct CdragonChampion {
    #[serde(default)]
    skins: Vec<CdragonSkin>,
}

#[derive(Debug, Deserialize)]
struct CdragonSkin {
    #[serde(default)]
    chromas: Vec<CdragonChroma>,
}

#[derive(Debug, Deserialize)]
struct CdragonChroma {
    id: u64,
    #[serde(default)]
    colors: Vec<String>,
}

/// Full chroma id → swatch colors, deduplicated (`[a, a]` becomes `[a]`).
fn chroma_colors(detail: CdragonChampion) -> HashMap<u64, Vec<String>> {
    detail
        .skins
        .into_iter()
        .flat_map(|s| s.chromas)
        .map(|c| {
            let mut colors: Vec<String> = c.colors.iter().map(|c| c.to_ascii_uppercase()).take(2).collect();
            colors.dedup();
            (c.id, colors)
        })
        .collect()
}

/// `"春晖神女 拉克丝 玉露金风"` under `"春晖神女 拉克丝"` → `"玉露金风"`. Words shared
/// with the parent (or the champion's name) are dropped from the front, and a
/// trailing champion name is dropped too.
fn chroma_name(full: &str, parent: &str, champion: &str) -> String {
    let known: Vec<&str> = parent.split_whitespace().chain([champion]).collect();
    let words: Vec<&str> = full.split_whitespace().collect();
    let skip = words.iter().take_while(|w| known.contains(w)).count();
    let mut rest = &words[skip..];
    // "这货不是边牧 库奇" under "柯基 库奇": the champion name can also trail.
    if rest.len() > 1 && rest.last() == Some(&champion) {
        rest = &rest[..rest.len() - 1];
    }
    let rest = rest.join(" ");
    if rest.is_empty() { full.to_string() } else { rest }
}

/// Portrait reference.
fn portrait_art(alias: &str) -> String {
    art::reference("tencent", &format!("champion/{alias}.png")).unwrap_or_default()
}

/// Card art reference. Newer skins exist only under the random id the source
/// gives in `loadingImg`; older ones also under the numeric path.
fn skin_art(raw: &RawSkin, full: u64) -> String {
    raw.loading_img
        .as_deref()
        .and_then(art::from_url)
        .or_else(|| art::reference("tencent", &format!("skinloading/{full}.jpg")))
        .unwrap_or_default()
}

/// Model render reference. Only older chromas have one; the UI hides the rest.
fn chroma_art(hero: u64, full: u64) -> String {
    art::reference("tencent", &format!("chromas/{hero}/{full}.png")).unwrap_or_default()
}

fn get(agent: &ureq::Agent, url: &str) -> Result<String> {
    let response = agent.get(url).set("User-Agent", "SkinLab-Catalog/0.4").call()?;
    let mut body = String::new();
    response.into_reader().take(MAX_BODY + 1).read_to_string(&mut body)?;
    if body.len() as u64 > MAX_BODY {
        bail!("response from {url} exceeds limit");
    }
    Ok(body)
}

/// Chroma variants are separate records in this source; nest them under the
/// skin they belong to. `colors` maps full chroma ids to swatch colors.
fn parse_champion(item: &HeroItem, detail: HeroDetail, colors: &HashMap<u64, Vec<String>>) -> Result<Champion> {
    let hero: u64 = item.hero_id.parse().context("invalid hero id")?;
    let number = |id: &str| -> Result<(u64, u32)> {
        let full: u64 = id.parse().context("invalid skin id")?;
        let number = full.checked_sub(hero * 1000).context("skin id below champion range")?;
        Ok((full, u32::try_from(number)?))
    };
    let parent_of = |s: &RawSkin| s.chromas_belong_id.clone().filter(|p| p != "0");
    let champion_name = item.title.clone().unwrap_or_default();
    let mut skins = Vec::new();
    for raw in detail.skins.iter().filter(|s| parent_of(s).is_none()) {
        let rarity = raw.emblems_name.clone().unwrap_or_default();
        let mut chromas = Vec::new();
        for chroma in detail.skins.iter().filter(|c| parent_of(c).as_deref() == Some(&raw.skin_id)) {
            let (full, skin_id) = number(&chroma.skin_id)?;
            chromas.push(Chroma {
                skin_id,
                name: chroma_name(&chroma.name, &raw.name, &champion_name),
                colors: colors.get(&full).cloned().unwrap_or_default(),
                art: chroma_art(hero, full),
            });
        }
        chromas.sort_by_key(|c| c.skin_id);
        let (full, skin_id) = number(&raw.skin_id)?;
        skins.push(Skin {
            skin_id,
            name: raw.name.clone(),
            art: skin_art(raw, full),
            chromas,
            requires_special_handling: rarity.eq_ignore_ascii_case("ultimate").then_some(true),
            rarity,
        });
    }
    skins.sort_by_key(|s| s.skin_id);
    let title = item.title.clone().unwrap_or_else(|| item.name.clone());
    Ok(Champion {
        id: item.alias.clone(),
        key: item.hero_id.clone(),
        name: format!("{title} · {}", item.name),
        art: portrait_art(&item.alias),
        skins,
    })
}

pub fn update(root: &Path, args: &[String]) -> Result<()> {
    let output = crate::flag(args, "--output").map(PathBuf::from).unwrap_or_else(|| root.join("data/catalog.json"));
    let tls = std::sync::Arc::new(native_tls::TlsConnector::new()?);
    let agent = ureq::AgentBuilder::new().tls_connector(tls).timeout(Duration::from_secs(10)).build();
    let index: HeroList = serde_json::from_str(&get(&agent, &format!("{TENCENT}heroList/hero_list.js"))?)?;
    if !(1..=500).contains(&index.hero.len()) {
        bail!("unexpected champion count {}", index.hero.len());
    }

    let (sender, results) = channel();
    let queue = std::sync::Arc::new(std::sync::Mutex::new(index.hero.clone()));
    std::thread::scope(|scope| {
        for _ in 0..WORKERS {
            let (queue, sender, agent) = (queue.clone(), sender.clone(), agent.clone());
            scope.spawn(move || {
                while let Some(item) = queue.lock().unwrap().pop() {
                    let result = (|| {
                        if item.hero_id.is_empty()
                            || item.hero_id.len() > 6
                            || !item.hero_id.bytes().all(|b| b.is_ascii_digit())
                        {
                            bail!("invalid champion key {}", item.hero_id);
                        }
                        let detail =
                            serde_json::from_str(&get(&agent, &format!("{TENCENT}hero/{}.js", item.hero_id))?)?;
                        // Swatch colors are optional: without them the UI paints a
                        // neutral swatch.
                        let colors = get(&agent, &format!("{CDRAGON}{}.json", item.hero_id))
                            .ok()
                            .and_then(|body| serde_json::from_str(&body).ok())
                            .map(chroma_colors)
                            .unwrap_or_default();
                        parse_champion(&item, detail, &colors)
                    })();
                    if sender.send(result).is_err() {
                        return;
                    }
                }
            });
        }
    });
    drop(sender);
    let mut champions = results.into_iter().collect::<Result<Vec<_>>>()?;
    champions.sort_by(|a, b| a.id.cmp(&b.id));

    let catalog = Catalog {
        schema_version: SCHEMA_VERSION,
        version: index.version,
        locale: "zh_CN".into(),
        source: TENCENT.into(),
        source_file_time: index.file_time,
        updated_at_utc: format!(
            "unix:{}",
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH)?.as_secs()
        ),
        champions,
    };
    catalog.validate()?;
    let temporary = output.with_extension("json.tmp");
    std::fs::write(&temporary, serde_json::to_string_pretty(&catalog)? + "\n")?;
    std::fs::rename(&temporary, &output)?;
    println!(
        "catalog {}: {} champions, {} skins, {} chromas",
        catalog.version,
        catalog.champions.len(),
        catalog.skin_count(),
        catalog.chroma_count(),
    );
    crate::native::catalog_header(root)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn smolder() -> HeroItem {
        HeroItem {
            hero_id: "901".into(), name: "小火龙".into(), alias: "Smolder".into(), title: Some("斯莫德".into())
        }
    }

    #[test]
    fn chromas_nest_under_their_skin() {
        let detail: HeroDetail = serde_json::from_str(
            r#"{"skins":[{"skinId":"901000","name":"原皮","chromasBelongId":"0","emblemsName":"base"},
                {"skinId":"901001","name":"天龙之子 斯莫德","chromasBelongId":"0","emblemsName":"Ultimate",
                 "loadingImg":"https://game.gtimg.cn/images/lol/act/img/skinloading/0d4018bf-0288.jpg"},
                {"skinId":"901003","name":"天龙之子 斯莫德 翡翠","chromasBelongId":"901001"},
                {"skinId":"901002","name":"天龙之子 斯莫德 红宝石","chromasBelongId":"901001"}]}"#,
        )
        .unwrap();
        let colors = HashMap::from([(901_002, vec!["#D33528".to_string()])]);
        let champion = parse_champion(&smolder(), detail, &colors).unwrap();
        assert_eq!(champion.name, "斯莫德 · 小火龙");
        assert_eq!(champion.art, "tencent:champion/Smolder.png");
        assert_eq!(champion.skins.iter().map(|s| s.skin_id).collect::<Vec<_>>(), [0, 1]);
        assert_eq!(champion.skins[0].art, "tencent:skinloading/901000.jpg");
        assert_eq!(champion.skins[1].art, "tencent:skinloading/0d4018bf-0288.jpg");
        assert_eq!(champion.skins[1].requires_special_handling, Some(true));
        let chromas = &champion.skins[1].chromas;
        assert_eq!(
            chromas.iter().map(|c| (c.skin_id, c.name.as_str())).collect::<Vec<_>>(),
            [(2, "红宝石"), (3, "翡翠")]
        );
        assert_eq!(chromas[0].colors, ["#D33528"]);
        assert_eq!(chromas[0].art, "tencent:chromas/901/901002.png");
        assert!(chromas[1].colors.is_empty());
    }

    #[test]
    fn skin_art_prefers_the_official_link() {
        let raw = |link: &str| RawSkin {
            skin_id: "238049".into(),
            name: "x".into(),
            chromas_belong_id: None,
            emblems_name: None,
            loading_img: Some(link.into()),
        };
        assert_eq!(
            skin_art(&raw("https://game.gtimg.cn/images/lol/act/img/skinloading/2e8229a8.jpg"), 238_049),
            "tencent:skinloading/2e8229a8.jpg"
        );
        assert_eq!(skin_art(&raw("https://elsewhere.example/x.jpg"), 238_049), "tencent:skinloading/238049.jpg");
    }

    #[test]
    fn chroma_names_drop_parent_words() {
        assert_eq!(chroma_name("春晖神女 拉克丝 玉露金风", "春晖神女 拉克丝", "拉克丝"), "玉露金风");
        assert_eq!(chroma_name("“闪耀灯球” 拉克丝 我又开团了", "太空律动 “闪耀灯球” 拉克丝", "拉克丝"), "我又开团了");
        assert_eq!(chroma_name("这货不是边牧 库奇", "柯基 库奇", "库奇"), "这货不是边牧");
        assert_eq!(chroma_name("至高天 拉克丝", "至高天 拉克丝", "拉克丝"), "至高天 拉克丝");
    }

    #[test]
    fn swatch_colors_are_normalized() {
        let detail: CdragonChampion = serde_json::from_str(
            r##"{"skins":[{"chromas":[{"id":99009,"colors":["#d33528","#D33528"]},{"id":99071,"colors":["#FF9AC7","#5ADADC"]}]},{}]}"##,
        )
        .unwrap();
        let colors = chroma_colors(detail);
        assert_eq!(colors[&99009], ["#D33528"]);
        assert_eq!(colors[&99071], ["#FF9AC7", "#5ADADC"]);
    }
}
