//! Parsing for the game's local Live Client API (`https://127.0.0.1:2999`).

use serde::{Deserialize, Serialize};

pub const LIVE_CLIENT_BASE: &str = "https://127.0.0.1:2999/liveclientdata/";
const MODEL_PREFIX: &str = "game_character_displayname_";

#[derive(Debug, Clone, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct Player {
    #[serde(default)]
    pub summoner_name: Option<String>,
    #[serde(default)]
    pub riot_id: Option<String>,
    #[serde(default)]
    pub raw_champion_name: String,
    #[serde(default)]
    pub champion_name: String,
    #[serde(default, rename = "skinID")]
    pub skin_id: Option<u32>,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
pub struct ActiveHero {
    pub model: String,
    pub display_name: String,
    pub skin_id: Option<u32>,
}

/// Picks the local player's champion. Returns `None` rather than guessing when
/// the active player is not in the list (e.g. still loading).
pub fn active_hero(active_name: &str, players: &[Player]) -> Option<ActiveHero> {
    players
        .iter()
        .filter(|p| p.summoner_name.as_deref() == Some(active_name) || p.riot_id.as_deref() == Some(active_name))
        .find_map(|p| {
            let model = p.raw_champion_name.strip_prefix(MODEL_PREFIX)?;
            (!model.is_empty()).then(|| ActiveHero {
                model: model.to_string(),
                display_name: p.champion_name.clone(),
                skin_id: p.skin_id,
            })
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn player(name: &str, model: &str) -> Player {
        Player {
            summoner_name: Some(name.into()),
            raw_champion_name: format!("{MODEL_PREFIX}{model}"),
            ..Default::default()
        }
    }

    #[test]
    fn selects_active_player_not_first() {
        let players = [player("bot", "Annie"), player("self", "Lux")];
        assert_eq!(active_hero("self", &players).unwrap().model, "Lux");
    }

    #[test]
    fn accepts_riot_id() {
        let players: Vec<Player> = serde_json::from_str(
            r#"[{"riotId":"name#tag","rawChampionName":"game_character_displayname_Smolder","skinID":1}]"#,
        )
        .unwrap();
        let hero = active_hero("name#tag", &players).unwrap();
        assert_eq!((hero.model.as_str(), hero.skin_id), ("Smolder", Some(1)));
    }

    #[test]
    fn no_guess_when_active_player_missing() {
        assert!(active_hero("self", &[player("bot", "Lux")]).is_none());
    }
}
