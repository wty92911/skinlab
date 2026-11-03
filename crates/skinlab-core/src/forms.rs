// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Skins whose extra forms have a verified native path. A form is a model
//! alias the session can push as an extra character stack layer; the host
//! announces which aliases it supports, so this table only names and orders
//! them. Add an entry only after the native side is verified in training mode.

#[derive(Debug, PartialEq, Eq)]
pub struct FormSet {
    pub champion: &'static str,
    pub skin: u32,
    /// UI label for the picker, e.g. `元素形态`.
    pub label: &'static str,
    /// First entry is the skin's own model (no extra layer).
    pub forms: &'static [Form],
}

#[derive(Debug, PartialEq, Eq)]
pub struct Form {
    /// Model alias sent as `form <id>`; must match the native table.
    pub id: &'static str,
    pub name: &'static str,
}

const fn form(id: &'static str, name: &'static str) -> Form {
    Form { id, name }
}

pub const FORM_SETS: &[FormSet] = &[FormSet {
    champion: "Lux",
    skin: 7,
    label: "元素形态",
    // Same order as native/loader/lux_forms.h.
    forms: &[
        form("Lux", "光"),
        form("LuxAir", "风"),
        form("LuxDark", "暗"),
        form("LuxFire", "火"),
        form("LuxIce", "冰"),
        form("LuxMagma", "熔岩"),
        form("LuxMystic", "秘术"),
        form("LuxNature", "自然"),
        form("LuxStorm", "风暴"),
        form("LuxWater", "水"),
    ],
}];

pub fn form_set(champion: &str, skin: u32) -> Option<&'static FormSet> {
    FORM_SETS.iter().find(|set| set.skin == skin && set.champion.eq_ignore_ascii_case(champion))
}

impl FormSet {
    pub fn form(&self, id: &str) -> Option<&'static Form> {
        self.forms.iter().find(|form| form.id == id)
    }

    pub fn base(&self) -> &'static Form {
        &self.forms[0]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lux_table_matches_native_aliases() {
        let header = include_str!("../../../native/loader/lux_forms.h");
        let start = header.find("sl_lux_forms[]={").expect("native alias table") + "sl_lux_forms[]={".len();
        let end = start + header[start..].find("};").unwrap();
        let native: Vec<&str> = header[start..end].split(',').map(|s| s.trim().trim_matches('"')).collect();
        let ours: Vec<&str> = form_set("lux", 7).unwrap().forms.iter().map(|f| f.id).collect();
        assert_eq!(native, ours);
    }

    #[test]
    fn lookup_is_per_skin() {
        assert!(form_set("Lux", 0).is_none());
        assert_eq!(form_set("Lux", 7).unwrap().base().id, "Lux");
        assert!(form_set("Lux", 7).unwrap().form("LuxAir").is_some());
        assert!(form_set("Lux", 7).unwrap().form("Ahri").is_none());
    }
}
