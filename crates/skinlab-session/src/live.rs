// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Reads the game's local Live Client API. Loopback only; never touches the game process.

use skinlab_core::live::{ActiveHero, LIVE_CLIENT_BASE, Player, active_hero};
use std::time::Duration;

const MAX_BODY: usize = 1024 * 1024;

pub struct LiveClient {
    http: Option<reqwest::blocking::Client>,
}

impl Default for LiveClient {
    fn default() -> Self {
        // The game serves a self-signed certificate on 127.0.0.1; bypassing the
        // system proxy keeps this traffic on the loopback interface.
        let http = reqwest::blocking::Client::builder()
            .danger_accept_invalid_certs(true)
            .no_proxy()
            .timeout(Duration::from_millis(800))
            .build()
            .ok();
        LiveClient { http }
    }
}

impl LiveClient {
    fn get(&self, endpoint: &str) -> Option<String> {
        let response = self.http.as_ref()?.get(format!("{LIVE_CLIENT_BASE}{endpoint}")).send().ok()?;
        if !response.status().is_success() {
            return None;
        }
        let body = response.bytes().ok()?;
        (body.len() <= MAX_BODY).then(|| String::from_utf8_lossy(&body).into_owned())
    }

    pub fn active_hero(&self) -> Option<ActiveHero> {
        let name: String = serde_json::from_str(&self.get("activeplayername")?).ok()?;
        let players: Vec<Player> = serde_json::from_str(&self.get("playerlist")?).ok()?;
        active_hero(&name, &players)
    }
}
