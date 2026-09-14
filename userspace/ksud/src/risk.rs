use crate::assets;
use crate::defs;
use const_format::concatcp;
use log::warn;
use serde::Deserialize;
use std::path::Path;
use std::process::Command;
use unicode_normalization::UnicodeNormalization;

const DEFAULT_RISK_JSON: &str = include_str!("../../../risk/risk.json");
const REMOTE_RISK_URL: &str =
    "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next/risk/risk/risk.json";
const RISK_CACHE_PATH: &str = concatcp!(defs::CACHE_DIR, "risk.json");

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
        return true;
    };
    let Ok(remote) = parse_risk_catalog(remote_json) else {
        return true;
    };

    local.hash != remote.hash
}

fn fetch_remote_risk_json() -> Option<Vec<u8>> {
    let output = Command::new(assets::BUSYBOX_PATH)
        .args(["wget", "-q", "-O", "-", "--", REMOTE_RISK_URL])
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
    let remote_bytes = fetch_remote_risk_json();

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

pub fn contains_risk(module_prop: &str) -> Option<RiskMatch> {
    let risk_json = load_risk_json();
    let risk: Vec<RiskGroup> = match parse_risk_catalog(&risk_json) {
        Ok(catalog) => catalog.rules,
        Err(err) => {
            warn!("Failed to parse risk catalog from cache: {err}. Falling back to bundled rules.");
            serde_json::from_str::<RiskCatalog>(DEFAULT_RISK_JSON)
                .map(|catalog| catalog.rules)
                .unwrap_or_default()
        }
    };

    let normalized_properties: Vec<String> = normalize_risk_text(module_prop)
        .split_whitespace()
        .map(str::to_owned)
        .collect();

    risk.iter().find_map(|group| {
        group.patterns.iter().find_map(|pattern| {
            let normalized_pattern: Vec<String> = normalize_risk_text(pattern)
                .split_whitespace()
                .map(str::to_owned)
                .collect();
            (!normalized_pattern.is_empty()
                && normalized_properties
                    .windows(normalized_pattern.len())
                    .any(|window| window == normalized_pattern.as_slice()))
                .then(|| RiskMatch {
                    reason: group.reason.clone(),
                    severity: group.severity,
                })
        })
    })
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
    fn timeout_block_omits_duplicate_risk_details() {
        let message = build_risk_timeout_block_message();

        assert!(message.contains("Installation Blocked"));
        assert!(!message.contains("Severity:"));
        assert!(!message.contains("Reason:"));
    }
}
