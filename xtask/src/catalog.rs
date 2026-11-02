//! Catalog refresh from the CN website's public champion data. The result is
//! reviewed and shipped with a release; the app never fetches it at runtime.

use anyhow::{Context, Result, bail};
use serde::Deserialize;
use skinlab_core::{Catalog, Champion, Skin};
use std::collections::HashSet;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::sync::mpsc::channel;
use std::time::Duration;

const TENCENT: &str = "https://game.gtimg.cn/images/lol/act/img/js/";
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

/// Chroma variants are separate records in this source; keep the base skin and
/// record that it has variants.
fn parse_champion(item: &HeroItem, detail: HeroDetail) -> Result<Champion> {
    let hero: u64 = item.hero_id.parse().context("invalid hero id")?;
    let belongs = |s: &RawSkin| s.chromas_belong_id.clone().unwrap_or_else(|| "0".into());
    let parents: HashSet<String> = detail.skins.iter().map(belongs).filter(|p| p != "0").collect();
    let mut skins = Vec::new();
    for raw in &detail.skins {
        if belongs(raw) != "0" {
            continue;
        }
        let full: u64 = raw.skin_id.parse().context("invalid skin id")?;
        let number = full.checked_sub(hero * 1000).context("skin id below champion range")?;
        let rarity = raw.emblems_name.clone().unwrap_or_default();
        skins.push(Skin {
            skin_id: u32::try_from(number)?,
            name: raw.name.clone(),
            has_chroma_variants: parents.contains(&raw.skin_id),
            requires_special_handling: rarity.eq_ignore_ascii_case("ultimate").then_some(true),
            rarity,
        });
    }
    skins.sort_by_key(|s| s.skin_id);
    let title = item.title.clone().unwrap_or_else(|| item.name.clone());
    Ok(Champion { id: item.alias.clone(), key: item.hero_id.clone(), name: format!("{title} · {}", item.name), skins })
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
                        parse_champion(&item, detail)
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
        schema_version: 1,
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
    println!("catalog {}: {} champions, {} skins", catalog.version, catalog.champions.len(), catalog.skin_count());
    crate::native::catalog_header(root)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn chroma_variants_do_not_remove_base_skin() {
        let item = HeroItem {
            hero_id: "901".into(),
            name: "小火龙".into(),
            alias: "Smolder".into(),
            title: Some("斯莫德".into()),
        };
        let detail: HeroDetail = serde_json::from_str(
            r#"{"skins":[{"skinId":"901000","name":"原皮","chromasBelongId":"0","emblemsName":"base"},
                {"skinId":"901001","name":"基础款式","chromasBelongId":"0","emblemsName":"Ultimate"},
                {"skinId":"901002","name":"炫彩变体","chromasBelongId":"901001"}]}"#,
        )
        .unwrap();
        let champion = parse_champion(&item, detail).unwrap();
        assert_eq!(champion.name, "斯莫德 · 小火龙");
        assert_eq!(champion.skins.iter().map(|s| s.skin_id).collect::<Vec<_>>(), [0, 1]);
        assert!(champion.skins[1].has_chroma_variants);
        assert_eq!(champion.skins[1].requires_special_handling, Some(true));
    }
}
