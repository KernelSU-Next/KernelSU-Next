use crate::utils::ensure_dir_exists;
use crate::{defs, ksu_uapi, ksucalls, sepolicy};
use anyhow::{Context, Result, bail};
use std::path::Path;

pub fn set_sepolicy(pkg: String, policy: String) -> Result<()> {
    ensure_dir_exists(defs::PROFILE_SELINUX_DIR)?;
    let policy_file = Path::new(defs::PROFILE_SELINUX_DIR).join(pkg);
    std::fs::write(&policy_file, policy)?;
    sepolicy::apply_file(&policy_file)?;
    Ok(())
}

pub fn get_sepolicy(pkg: String) -> Result<()> {
    let policy_file = Path::new(defs::PROFILE_SELINUX_DIR).join(pkg);
    let policy = std::fs::read_to_string(policy_file)?;
    println!("{policy}");
    Ok(())
}

// ksud doesn't guarteen the correctness of template, it just save
pub fn set_template(id: String, template: String) -> Result<()> {
    ensure_dir_exists(defs::PROFILE_TEMPLATE_DIR)?;
    let template_file = Path::new(defs::PROFILE_TEMPLATE_DIR).join(id);
    std::fs::write(template_file, template)?;
    Ok(())
}

pub fn get_template(id: String) -> Result<()> {
    let template_file = Path::new(defs::PROFILE_TEMPLATE_DIR).join(id);
    let template = std::fs::read_to_string(template_file)?;
    println!("{template}");
    Ok(())
}

pub fn delete_template(id: String) -> Result<()> {
    let template_file = Path::new(defs::PROFILE_TEMPLATE_DIR).join(id);
    std::fs::remove_file(template_file)?;
    Ok(())
}

pub fn list_templates() -> Result<()> {
    let templates = std::fs::read_dir(defs::PROFILE_TEMPLATE_DIR);
    let Ok(templates) = templates else {
        return Ok(());
    };
    for template in templates {
        let template = template?;
        let template = template.file_name();
        if let Some(template) = template.to_str() {
            println!("{template}");
        }
    }
    Ok(())
}

pub fn apply_sepolies() -> Result<()> {
    let path = Path::new(defs::PROFILE_SELINUX_DIR);
    if !path.exists() {
        log::info!("profile sepolicy dir not exists.");
        return Ok(());
    }

    let sepolicies =
        std::fs::read_dir(path).with_context(|| "profile sepolicy dir open failed.".to_string())?;
    for sepolicy in sepolicies {
        let Ok(sepolicy) = sepolicy else {
            log::info!("profile sepolicy dir read failed.");
            continue;
        };
        let sepolicy = sepolicy.path();
        if sepolicy::apply_file(&sepolicy).is_ok() {
            log::info!("profile sepolicy applied: {}", sepolicy.display());
        } else {
            log::info!("profile sepolicy apply failed: {}", sepolicy.display());
        }
    }
    Ok(())
}

/// The profile the kernel keeps for one app, as JSON.
///
/// The kernel structure is a union: `allow_su` decides which half of it means anything, so only
/// the matching section is written out and only the matching one may be given back.
#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct AppProfile {
    /// Usually the package name; the kernel uses it for its own bookkeeping and looks profiles
    /// up by uid.
    pub key: String,

    /// The app's current uid. Left out, it is looked up from the package name.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub uid: Option<i32>,

    /// False when the kernel holds no profile for this app and what follows is only what it
    /// would fall back on. Ignored on input.
    #[serde(default, skip_deserializing)]
    pub stored: bool,

    pub allow_su: bool,

    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub root: Option<RootProfile>,

    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub non_root: Option<NonRootProfile>,
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct RootProfile {
    /// Take the values from the default root profile rather than the ones below.
    pub use_default: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub template: Option<String>,
    pub uid: i32,
    pub gid: i32,
    pub groups: Vec<i32>,
    /// Capability names, with or without the CAP_ prefix; a bare number is also accepted.
    pub capabilities: Vec<String>,
    pub selinux_domain: String,
    pub namespace: Namespace,
    pub flags: Vec<String>,
}

#[derive(serde::Serialize, serde::Deserialize, Debug)]
pub struct NonRootProfile {
    /// Take the value from the default non-root profile rather than the one below.
    pub use_default: bool,
    pub umount_modules: bool,
}

#[derive(serde::Serialize, serde::Deserialize, Debug, Clone, Copy, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Namespace {
    Inherited,
    Global,
    Individual,
}

impl Namespace {
    fn from_kernel(value: i32) -> Result<Self> {
        match value {
            0 => Ok(Self::Inherited),
            1 => Ok(Self::Global),
            2 => Ok(Self::Individual),
            other => bail!("the kernel reported namespace {other}, which this build does not know"),
        }
    }

    const fn to_kernel(self) -> i32 {
        match self {
            Self::Inherited => 0,
            Self::Global => 1,
            Self::Individual => 2,
        }
    }
}

/// Capability names by bit, in the order the kernel numbers them.
const CAPABILITIES: [&str; 41] = [
    "CHOWN",
    "DAC_OVERRIDE",
    "DAC_READ_SEARCH",
    "FOWNER",
    "FSETID",
    "KILL",
    "SETGID",
    "SETUID",
    "SETPCAP",
    "LINUX_IMMUTABLE",
    "NET_BIND_SERVICE",
    "NET_BROADCAST",
    "NET_ADMIN",
    "NET_RAW",
    "IPC_LOCK",
    "IPC_OWNER",
    "SYS_MODULE",
    "SYS_RAWIO",
    "SYS_CHROOT",
    "SYS_PTRACE",
    "SYS_PACCT",
    "SYS_ADMIN",
    "SYS_BOOT",
    "SYS_NICE",
    "SYS_RESOURCE",
    "SYS_TIME",
    "SYS_TTY_CONFIG",
    "MKNOD",
    "LEASE",
    "AUDIT_WRITE",
    "AUDIT_CONTROL",
    "SETFCAP",
    "MAC_OVERRIDE",
    "MAC_ADMIN",
    "SYSLOG",
    "WAKE_ALARM",
    "BLOCK_SUSPEND",
    "AUDIT_READ",
    "PERFMON",
    "BPF",
    "CHECKPOINT_RESTORE",
];

/// The only root profile flag the kernel defines, by bit.
const FLAGS: [&str; 1] = ["NO_NEW_PRIVS"];

fn names_from_bits(bits: u64, names: &[&str]) -> Vec<String> {
    let mut out: Vec<String> = names
        .iter()
        .enumerate()
        .filter(|(bit, _)| bits & (1u64 << bit) != 0)
        .map(|(_, name)| (*name).to_string())
        .collect();
    // A bit the kernel knows and this build does not would otherwise vanish on a round trip.
    for bit in names.len()..64 {
        if bits & (1u64 << bit) != 0 {
            out.push(bit.to_string());
        }
    }
    out
}

fn bits_from_names(values: &[String], names: &[&str], what: &str) -> Result<u64> {
    let mut bits = 0u64;
    for value in values {
        let wanted = value.trim().trim_start_matches("CAP_").to_uppercase();
        let bit = if let Ok(bit) = wanted.parse::<u32>() {
            bit
        } else {
            names
                .iter()
                .position(|name| *name == wanted)
                .with_context(|| format!("no {what} called \"{value}\""))? as u32
        };
        if bit >= 64 {
            bail!("{what} {value} is out of range");
        }
        bits |= 1u64 << bit;
    }
    Ok(bits)
}

/// Read a kernel profile into the shape that is printed.
fn from_kernel(profile: &ksu_uapi::app_profile, stored: bool) -> Result<AppProfile> {
    let key = ksucalls::read_c_str(&profile.key);
    let allow_su = profile.allow_su;

    // Reading the half the union is not holding would be reading rubbish, so allow_su picks.
    let (root, non_root) = if allow_su {
        let rp = unsafe { profile.__bindgen_anon_1.rp_config };
        let template = ksucalls::read_c_str(&rp.template_name);
        let groups_count = (rp.profile.groups_count as usize).min(rp.profile.groups.len());
        (
            Some(RootProfile {
                use_default: rp.use_default,
                template: (!template.is_empty()).then_some(template),
                uid: rp.profile.uid,
                gid: rp.profile.gid,
                groups: rp.profile.groups[..groups_count].to_vec(),
                // The manager writes only the effective set, and the kernel derives the rest.
                capabilities: names_from_bits(rp.profile.capabilities.effective, &CAPABILITIES),
                selinux_domain: ksucalls::read_c_str(&rp.profile.selinux_domain),
                namespace: Namespace::from_kernel(rp.profile.namespaces)?,
                flags: names_from_bits(rp.profile.flags, &FLAGS),
            }),
            None,
        )
    } else {
        let nrp = unsafe { profile.__bindgen_anon_1.nrp_config };
        (
            None,
            Some(NonRootProfile {
                use_default: nrp.use_default,
                umount_modules: nrp.profile.umount_modules,
            }),
        )
    };

    Ok(AppProfile {
        key,
        uid: Some(profile.curr_uid),
        stored,
        allow_su,
        root,
        non_root,
    })
}

/// Lay one out for the kernel. `allow_su` decides which half of the union is filled in.
fn to_kernel(wanted: &AppProfile, uid: i32) -> Result<ksu_uapi::app_profile> {
    let mut profile = ksucalls::new_profile(&wanted.key, uid)?;
    profile.allow_su = wanted.allow_su;

    match (wanted.allow_su, &wanted.root, &wanted.non_root) {
        (_, Some(_), Some(_)) => bail!("provide only the profile section selected by allow_su"),
        (true, None, _) => bail!("allow_su requires a root section"),
        (false, _, None) => bail!("allow_su=false requires a non_root section"),
        (true, Some(root), None) => {
            let mut rp: ksu_uapi::app_profile__bindgen_ty_1__bindgen_ty_1 =
                unsafe { std::mem::zeroed() };
            rp.use_default = root.use_default;
            if let Some(template) = &root.template {
                ksucalls::write_c_str(&mut rp.template_name, template).context("template")?;
            }
            rp.profile.uid = root.uid;
            rp.profile.gid = root.gid;
            if root.groups.len() > rp.profile.groups.len() {
                bail!(
                    "{} groups given, and the kernel keeps at most {}",
                    root.groups.len(),
                    rp.profile.groups.len()
                );
            }
            rp.profile.groups_count = root.groups.len() as u32;
            rp.profile.groups[..root.groups.len()].copy_from_slice(&root.groups);
            rp.profile.capabilities.effective =
                bits_from_names(&root.capabilities, &CAPABILITIES, "capability")?;
            ksucalls::write_c_str(&mut rp.profile.selinux_domain, &root.selinux_domain)
                .context("selinux domain")?;
            rp.profile.namespaces = root.namespace.to_kernel();
            rp.profile.flags = bits_from_names(&root.flags, &FLAGS, "flag")?;
            profile.__bindgen_anon_1.rp_config = rp;
        }
        (false, None, Some(non_root)) => {
            let mut nrp: ksu_uapi::app_profile__bindgen_ty_1__bindgen_ty_2 =
                unsafe { std::mem::zeroed() };
            nrp.use_default = non_root.use_default;
            nrp.profile.umount_modules = non_root.umount_modules;
            profile.__bindgen_anon_1.nrp_config = nrp;
        }
    }
    Ok(profile)
}

/// The key the kernel files the default non-root profile under, and the uid that goes with it.
/// Setting that profile is what the manager's global "umount modules" switch does.
const DEFAULT_NON_ROOT_KEY: &str = "$";
const DEFAULT_NON_ROOT_UID: i32 = 9999;

/// The uid Android currently gives a package.
///
/// The kernel files profiles by uid, while a person has a package name; /data/system/packages.list
/// is the mapping the system keeps for itself, one "<package> <uid> …" per line.
fn uid_of_package(package: &str) -> Result<i32> {
    let list = "/data/system/packages.list";
    let text = std::fs::read_to_string(list).with_context(|| format!("could not read {list}"))?;
    for line in text.lines() {
        let mut fields = line.split_whitespace();
        if fields.next() == Some(package) {
            let uid = fields.next().context("packages.list line has no uid")?;
            return uid
                .parse()
                .with_context(|| format!("{list}: \"{uid}\" is not a uid"));
        }
    }
    bail!("no package called \"{package}\" is installed; give --uid to address it anyway")
}

fn resolve_uid(key: &str, given: Option<i32>) -> Result<i32> {
    match (given, key) {
        (Some(uid), _) => Ok(uid),
        (None, DEFAULT_NON_ROOT_KEY) => Ok(DEFAULT_NON_ROOT_UID),
        (None, package) => uid_of_package(package),
    }
}

/// What the kernel falls back on for an app it holds no profile for.
fn fallback_profile(key: &str, uid: i32) -> Result<AppProfile> {
    let umount_modules = if key == DEFAULT_NON_ROOT_KEY {
        true
    } else {
        // Whatever the global default profile says, since that is what will apply.
        match ksucalls::get_app_profile(DEFAULT_NON_ROOT_KEY, DEFAULT_NON_ROOT_UID)? {
            Some(profile) if !profile.allow_su => {
                unsafe { profile.__bindgen_anon_1.nrp_config }
                    .profile
                    .umount_modules
            }
            _ => true,
        }
    };
    Ok(AppProfile {
        key: key.to_string(),
        uid: Some(uid),
        stored: false,
        allow_su: false,
        root: None,
        non_root: Some(NonRootProfile {
            use_default: true,
            umount_modules,
        }),
    })
}

/// Print the profile of one app as JSON.
pub fn get_profile(key: &str, uid: Option<i32>) -> Result<()> {
    let uid = resolve_uid(key, uid)?;
    let profile = match ksucalls::get_app_profile(key, uid)? {
        Some(profile) => from_kernel(&profile, true)?,
        None => fallback_profile(key, uid)?,
    };
    println!("{}", serde_json::to_string_pretty(&profile)?);
    Ok(())
}

/// Set the profile of one app from JSON, read from a file or from stdin.
pub fn set_profile(file: Option<&str>) -> Result<()> {
    let text = match file {
        Some("-") | None => {
            let mut text = String::new();
            std::io::Read::read_to_string(&mut std::io::stdin(), &mut text)
                .context("could not read the profile from stdin")?;
            text
        }
        Some(path) => {
            std::fs::read_to_string(path).with_context(|| format!("could not read {path}"))?
        }
    };

    let profile: AppProfile = serde_json::from_str(&text).context("could not parse the profile")?;
    let uid = resolve_uid(&profile.key, profile.uid)?;
    ksucalls::set_app_profile(&to_kernel(&profile, uid)?)
}
