use crate::assets;
use crate::defs;
use const_format::concatcp;
use log::warn;
use serde::Deserialize;
use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::process::Command;
use unicode_normalization::UnicodeNormalization;
use anyhow::Result;

const DEFAULT_RISK_JSON: &str = include_str!("../../../risk/risk.json");
const REMOTE_RISK_URL: &str =
    "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/risk/risk/risk.json";
const RISK_CACHE_PATH: &str = concatcp!(defs::CACHE_DIR, "risk.json");

const MAX_SCAN_BYTES_PER_FILE: u64 = 8 * 1024 * 1024;

const RISK_CONFIG_MODULE_ID: &str = "internal.ksud.risk";
const RISK_CONFIG_KEY: &str = "enabled";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum RiskSeverity {
    Low,
    Medium,
    High,
    Extreme,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct RiskGroup {
    pub reason: String,
    pub severity: RiskSeverity,
    pub patterns: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RiskMatch {
    pub reason: String,
    pub severity: RiskSeverity,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
struct RiskCatalog {
    hash: String,
    rules: Vec<RiskGroup>,
}

fn parse_risk_catalog(json: &[u8]) -> Result<RiskCatalog, serde_json::Error> {
    serde_json::from_slice(json)
}

pub fn should_update_risk_cache(local_json: &[u8], remote_json: &[u8]) -> bool {
    let Ok(local) = parse_risk_catalog(local_json) else {
        // Local cache is unreadable; only accept a *valid* remote catalog.
        return parse_risk_catalog(remote_json).is_ok();
    };
    let Ok(remote) = parse_risk_catalog(remote_json) else {
        // Remote is unreadable (e.g. a hijacked response or error page);
        // never let it replace a valid local cache.
        return false;
    };

    local.hash != remote.hash
}

fn fetch_remote_risk_json() -> Option<Vec<u8>> {
    // Wrap wget with the timeout applet so a blackholed/hijacked network
    // path cannot stall module installation indefinitely.
    let script = format!(
        "timeout 15 '{}' wget -q -O - -- '{}'",
        assets::BUSYBOX_PATH, REMOTE_RISK_URL
    );
    let output = Command::new(assets::BUSYBOX_PATH)
        .args(["ash", "-c", &script])
        .output()
        .ok()?;

    if !output.status.success() {
        warn!("Failed to fetch risk rules from {REMOTE_RISK_URL}");
        return None;
    }

    Some(output.stdout)
}

fn load_risk_json() -> Vec<u8> {
    let cache_path = Path::new(RISK_CACHE_PATH);
    if let Err(err) = crate::utils::ensure_dir_exists(defs::CACHE_DIR) {
        warn!("Failed to ensure risk cache dir exists: {err}");
    }

    let local_bytes = std::fs::read(cache_path).ok();
    // A hijacked/captive-portal response must never poison the on-disk
    // cache, so only accept remote bytes that parse as a valid catalog.
    let remote_bytes = fetch_remote_risk_json().filter(|bytes| parse_risk_catalog(bytes).is_ok());

    match (local_bytes, remote_bytes) {
        (Some(local), Some(remote)) => {
            if should_update_risk_cache(&local, &remote) {
                if let Err(err) = std::fs::write(cache_path, &remote) {
                    warn!("Failed to update risk cache at {}: {err}", cache_path.display());
                }
                remote
            } else {
                local
            }
        }
        (Some(local), None) => local,
        (None, Some(remote)) => {
            if let Err(err) = std::fs::write(cache_path, &remote) {
                warn!("Failed to write risk cache at {}: {err}", cache_path.display());
            }
            remote
        }
        (None, None) => DEFAULT_RISK_JSON.as_bytes().to_vec(),
    }
}

struct CompiledRule {
    reason: String,
    severity: RiskSeverity,
    patterns: Vec<Vec<String>>,
}

fn tokenize_risk_text(text: &str) -> Vec<String> {
    normalize_risk_text(text)
        .split_whitespace()
        .map(str::to_owned)
        .collect()
}

fn load_compiled_rules() -> Vec<CompiledRule> {
    let risk_json = load_risk_json();
    let groups: Vec<RiskGroup> = match parse_risk_catalog(&risk_json) {
        Ok(catalog) => catalog.rules,
        Err(err) => {
            warn!("Failed to parse risk catalog from cache: {err}. Falling back to bundled rules.");
            serde_json::from_str::<RiskCatalog>(DEFAULT_RISK_JSON)
                .map(|catalog| catalog.rules)
                .unwrap_or_default()
        }
    };

    groups
        .into_iter()
        .map(|group| CompiledRule {
            patterns: group
                .patterns
                .iter()
                .map(|pattern| tokenize_risk_text(pattern))
                .filter(|tokens| !tokens.is_empty())
                .collect(),
            reason: group.reason,
            severity: group.severity,
        })
        .collect()
}

fn match_rules(rules: &[CompiledRule], text: &str) -> Option<RiskMatch> {
    let tokens = tokenize_risk_text(text);
    rules.iter().find_map(|rule| {
        rule.patterns
            .iter()
            .any(|pattern| {
                tokens
                    .windows(pattern.len())
                    .any(|window| window == pattern.as_slice())
            })
            .then(|| RiskMatch {
                reason: rule.reason.clone(),
                severity: rule.severity,
            })
    })
}

pub fn contains_risk_in_module(zip_path: &Path) -> Option<RiskMatch> {
    let rules = load_compiled_rules();
    if rules.is_empty() {
        return None;
    }

    let file = match File::open(zip_path) {
        Ok(file) => file,
        Err(err) => {
            warn!("Risk scan: failed to open {}: {err}", zip_path.display());
            return None;
        }
    };
    let mut archive = match zip::ZipArchive::new(file) {
        Ok(archive) => archive,
        Err(err) => {
            warn!("Risk scan: failed to read zip {}: {err}", zip_path.display());
            return None;
        }
    };

    let mut buffer: Vec<u8> = Vec::new();
    for index in 0..archive.len() {
        let entry = match archive.by_index(index) {
            Ok(entry) => entry,
            Err(err) => {
                warn!("Risk scan: failed to open zip entry #{index}: {err}");
                continue;
            }
        };
        if !entry.is_file() {
            continue;
        }

        let name = entry.name().to_owned();
        buffer.clear();
        if let Err(err) = entry.take(MAX_SCAN_BYTES_PER_FILE).read_to_end(&mut buffer) {
            warn!("Risk scan: failed to read {name}: {err}");
            continue;
        }

        let text = String::from_utf8_lossy(&buffer);
        if let Some(risk_match) = match_rules(&rules, &text) {
            warn!("Risk rule hit in {name}: {}", risk_match.reason);
            return Some(risk_match);
        }
    }

    None
}

fn build_risk_block_message(severity: RiskSeverity, reason: &str) -> String {
    format!(
        "\n❌ Installation Blocked\n┌────────────────────────────────\n│ Module flagged by a security rule\n│\n│ Severity: {:?}\n│ Reason: {}\n└─────────────────────────────────\n",
        severity, reason
    )
}

pub fn print_risk_block(severity: RiskSeverity, reason: &str) {
    print!("{}", build_risk_block_message(severity, reason));
}

fn build_risk_timeout_block_message() -> String {
    "\n❌ Installation Stopped\n┌────────────────────────────────\n│ Permission confirmation timed out\n│ Module installation was not confirmed in time.\n└─────────────────────────────────\n"
        .to_owned()
}

pub fn print_risk_timeout_block() {
    print!("{}", build_risk_timeout_block_message());
}

fn build_risk_pause_prompt_message(severity: RiskSeverity, reason: &str) -> String {
    format!(
        "\n⚠️  Installation Paused\n┌────────────────────────────────\n│ Module flagged by a security rule\n│\n│ Severity: {:?}\n│ Reason: {}\n│\n│ Press the volume-down key within 5 seconds to continue.\n└─────────────────────────────────\n",
        severity, reason
    )
}

pub fn print_risk_pause_prompt(severity: RiskSeverity, reason: &str) {
    print!("{}", build_risk_pause_prompt_message(severity, reason));
}

fn normalize_risk_text(text: &str) -> String {
    text.nfkc()
        .flat_map(|character| character.to_lowercase())
        .map(|character| {
            if character.is_alphanumeric() {
                character
            } else {
                ' '
            }
        })
        .collect::<String>()
}

pub fn is_risk_detection_enabled() -> bool {
    match crate::module_config::get_config_value(
        RISK_CONFIG_MODULE_ID,
        RISK_CONFIG_KEY,
        crate::module_config::ConfigType::Persist,
    ) {
        Ok(Some(value)) => crate::module_config::parse_bool_config(&value),
        Ok(None) | Err(_) => true,
    }
}

pub fn set_risk_detection_enabled(enabled: bool) -> Result<()> {
    crate::module_config::set_config_value(
        RISK_CONFIG_MODULE_ID,
        RISK_CONFIG_KEY,
        &enabled.to_string(),
        crate::module_config::ConfigType::Persist,
    )?;

    if enabled {
        println!("Enabled");
    } else {
        println!("Disabled");
    }

    Ok(())
}

pub fn risk_detection_status() -> Result<()> {
    if is_risk_detection_enabled() {
        println!("Enabled");
    } else {
        println!("Disabled");
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn risk_cache_diff_detects_change() {
        let local = br#"{"hash":"abc","rules":[{"reason":"demo","severity":"low","patterns":["alpha"]}]}"#;
        let remote_same = br#"{"hash":"abc","rules":[{"reason":"demo","severity":"low","patterns":["alpha"]}]}"#;
        let remote_diff = br#"{"hash":"def","rules":[{"reason":"demo","severity":"low","patterns":["beta"]}]}"#;

        assert!(!should_update_risk_cache(local, remote_same));
        assert!(should_update_risk_cache(local, remote_diff));
    }

    #[test]
    fn garbage_remote_never_replaces_cache() {
        let local = br#"{"hash":"abc","rules":[{"reason":"demo","severity":"low","patterns":["alpha"]}]}"#;
        let garbage = b"<html>302 Found - redirect to carrier portal</html>";

        // Hijacked/garbage remote responses must not win over a valid local cache.
        assert!(!should_update_risk_cache(local, garbage));
    }

    #[test]
    fn valid_remote_can_replace_corrupted_local() {
        let garbage = b"not json at all";
        let remote = br#"{"hash":"abc","rules":[]}"#;

        assert!(should_update_risk_cache(garbage, remote));
        // Both corrupted: keep what we have rather than swapping garbage.
        assert!(!should_update_risk_cache(garbage, garbage));
    }

    #[test]
    fn timeout_block_omits_duplicate_risk_details() {
        let message = build_risk_timeout_block_message();

        assert!(message.contains("Installation Blocked"));
        assert!(!message.contains("Severity:"));
        assert!(!message.contains("Reason:"));
    }

    #[test]
    fn match_rules_finds_hit_in_arbitrary_text() {
        let rules = vec![CompiledRule {
            reason: "demo".to_owned(),
            severity: RiskSeverity::High,
            patterns: vec![tokenize_risk_text("evil payload")],
        }];

        let hit = match_rules(&rules, "#!/system/bin/sh\nrun EVIL-Payload now").unwrap();
        assert_eq!(hit.severity, RiskSeverity::High);
        assert!(match_rules(&rules, "evil and payload").is_none());
    }
}