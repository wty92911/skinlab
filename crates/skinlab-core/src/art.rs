// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
//! Image references. The catalog stores every image as `<origin>:<path>`; the
//! app only turns them into URLs and loads them on demand. Origins are fixed
//! here and mirrored by the app's CSP.

pub struct Origin {
    pub name: &'static str,
    pub base: &'static str,
}

/// The CN website's CDN is the only image source.
pub const ORIGINS: &[Origin] = &[Origin { name: "tencent", base: "https://game.gtimg.cn/images/lol/act/img/" }];

fn origin(name: &str) -> Option<&'static Origin> {
    ORIGINS.iter().find(|o| o.name == name)
}

fn valid_path(path: &str) -> bool {
    (1..=160).contains(&path.len())
        && !path.starts_with('/')
        && !path.contains("..")
        && path.bytes().all(|b| b.is_ascii_alphanumeric() || b"/._-".contains(&b))
}

/// `tencent:champion/Lux.png` → full URL. `None` for anything else.
pub fn url(reference: &str) -> Option<String> {
    let (name, path) = reference.split_once(':')?;
    let origin = origin(name)?;
    valid_path(path).then(|| format!("{}{path}", origin.base))
}

pub fn valid(reference: &str) -> bool {
    url(reference).is_some()
}

/// Builds a reference; `None` when the path is not acceptable.
pub fn reference(origin_name: &str, path: &str) -> Option<String> {
    let reference = format!("{origin_name}:{path}");
    valid(&reference).then_some(reference)
}

/// Full URL from a known origin → reference (used for source data that
/// carries absolute links).
pub fn from_url(url: &str) -> Option<String> {
    ORIGINS.iter().find_map(|o| reference(o.name, url.strip_prefix(o.base)?))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn references_round_trip() {
        let full = "https://game.gtimg.cn/images/lol/act/img/skinloading/0d4018bf-0288.jpg";
        let r = from_url(full).unwrap();
        assert_eq!(r, "tencent:skinloading/0d4018bf-0288.jpg");
        assert_eq!(url(&r).unwrap(), full);
    }

    #[test]
    fn only_known_origins_and_plain_paths() {
        for bad in [
            "evil:x.png",
            "cdragon:x.png",
            "tencent:../x.png",
            "tencent:/x.png",
            "tencent:x.png?a=1",
            "tencent:",
            "https://evil.example/x.png",
        ] {
            assert!(!valid(bad), "{bad}");
        }
        assert!(from_url("https://evil.example/images/lol/act/img/x.png").is_none());
    }

    #[test]
    fn csp_allows_every_origin() {
        let config = include_str!("../../../app/tauri.conf.json");
        let csp = config.split("img-src").nth(1).and_then(|s| s.split(';').next()).unwrap();
        for origin in ORIGINS {
            let host = origin.base.split('/').take(3).collect::<Vec<_>>().join("/");
            assert!(csp.split_whitespace().any(|s| s == host), "CSP img-src lacks {host}");
        }
    }
}
