use std::ffi::{CStr, CString};
use anyhow::{Context, Result, anyhow, ensure};
use libc::{syscall, SYS_reboot, c_char};
use log::{info, warn};

// Constants from susfsd.c
const KSU_INSTALL_MAGIC1: u64 = 0xDEADBEEF;
const SUSFS_MAGIC: u64 = 0xFAFAFAFA;

const CMD_SUSFS_SHOW_VERSION: u64 = 0x555e1;
const CMD_SUSFS_SHOW_ENABLED_FEATURES: u64 = 0x555e2;
const CMD_SUSFS_SHOW_VARIANT: u64 = 0x555e3;

const SUSFS_ENABLED_FEATURES_SIZE: usize = 8192;
const SUSFS_MAX_VERSION_BUFSIZE: usize = 16;
const SUSFS_MAX_VARIANT_BUFSIZE: usize = 16;

const ERR_CMD_NOT_SUPPORTED: i32 = 126;
const AUTOLOAD_CONFIG_MODULE_ID: &str = "internal.ksud.susfsd";
const AUTOLOAD_ENABLED_CONFIG_KEY: &str = "autoload.enabled";
const AUTOLOAD_FALLBACK_KMI_CONFIG_KEY: &str = "autoload.fallback_kmi";

struct AutoLoadConfig {
    enabled: bool,
    fallback_kmi: Option<String>,
}

fn validate_kmi(kmi: &str) -> Result<()> {
    ensure!(
        !kmi.is_empty()
            && kmi.len() <= 64
            && kmi.bytes().all(|byte| {
                byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'_' | b'-')
            }),
        "Invalid KMI '{kmi}'"
    );
    Ok(())
}

fn read_autoload_config() -> Result<AutoLoadConfig> {
    let enabled = crate::module_config::get_config_value(
        AUTOLOAD_CONFIG_MODULE_ID,
        AUTOLOAD_ENABLED_CONFIG_KEY,
        crate::module_config::ConfigType::Persist,
    )?;
    let fallback_kmi = crate::module_config::get_config_value(
        AUTOLOAD_CONFIG_MODULE_ID,
        AUTOLOAD_FALLBACK_KMI_CONFIG_KEY,
        crate::module_config::ConfigType::Persist,
    )?;
    if let Some(kmi) = &fallback_kmi {
        validate_kmi(kmi)?;
    }

    Ok(AutoLoadConfig {
        enabled: enabled
            .as_deref()
            .is_some_and(crate::module_config::parse_bool_config),
        fallback_kmi,
    })
}

pub fn autoload_enabled() -> Result<bool> {
    Ok(read_autoload_config()?.enabled)
}

pub fn set_autoload(enabled: bool, kmi: Option<&str>) -> Result<()> {
    if !enabled {
        crate::module_config::set_config_value(
            AUTOLOAD_CONFIG_MODULE_ID,
            AUTOLOAD_ENABLED_CONFIG_KEY,
            "false",
            crate::module_config::ConfigType::Persist,
        )
        .context("Failed to disable SUSFS auto-load")?;
        println!("SUSFS auto-load disabled");
        return Ok(());
    }

    anyhow::ensure!(
        cfg!(any(target_arch = "aarch64", target_arch = "x86_64")),
        "SUSFS guard LKM is only available for arm64 and x86_64"
    );

    let kmi = match kmi {
        Some(kmi) => kmi.to_owned(),
        None => crate::boot_patch::get_current_kmi().context(
            "Failed to detect kernel KMI; specify it with `ksud susfs autoload enable --kmi <KMI>`",
        )?,
    };
    validate_kmi(&kmi)?;
    let asset_name = format!("{kmi}_susfs_guard_lkm.ko");
    crate::assets::get_asset_data(&asset_name)
        .with_context(|| format!("SUSFS guard asset is unavailable for KMI {kmi}"))?;

    crate::module_config::set_config_value(
        AUTOLOAD_CONFIG_MODULE_ID,
        AUTOLOAD_FALLBACK_KMI_CONFIG_KEY,
        &kmi,
        crate::module_config::ConfigType::Persist,
    )
    .context("Failed to save SUSFS auto-load config")?;
    crate::module_config::set_config_value(
        AUTOLOAD_CONFIG_MODULE_ID,
        AUTOLOAD_ENABLED_CONFIG_KEY,
        "true",
        crate::module_config::ConfigType::Persist,
    )
    .context("Failed to save SUSFS auto-load config")?;

    println!("SUSFS auto-load enabled (KMI fallback: {kmi})");
    Ok(())
}

fn detected_susfs_version() -> Option<String> {
    let mut cmd = SusfsVersion {
        version: [0; SUSFS_MAX_VERSION_BUFSIZE],
        err: ERR_CMD_NOT_SUPPORTED,
    };
    unsafe {
        syscall(
            SYS_reboot,
            KSU_INSTALL_MAGIC1,
            SUSFS_MAGIC,
            CMD_SUSFS_SHOW_VERSION,
            &mut cmd as *mut _,
        );
    }
    if cmd.err != 0 {
        return None;
    }

    let length = cmd
        .version
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(cmd.version.len());
    (length != 0).then(|| String::from_utf8_lossy(&cmd.version[..length]).into_owned())
}

pub fn load_if_autoload_enabled() -> Result<()> {
    let config = read_autoload_config()?;
    if !config.enabled {
        return Ok(());
    }
    if !cfg!(any(target_arch = "aarch64", target_arch = "x86_64")) {
        warn!("SUSFS auto-load is enabled, but the SUSFS LKM is only available for arm64 and x86_64");
        return Ok(());
    }
    if let Some(version) = detected_susfs_version() {
        info!("SUSFS {version} is already available; skipping guard LKM load");
        return Ok(());
    }

    let kmi = crate::boot_patch::get_current_kmi()
        .ok()
        .or(config.fallback_kmi);
    let Some(kmi) = kmi else {
        warn!("SUSFS auto-load is enabled, but KMI detection failed and no fallback KMI is saved");
        return Ok(());
    };

    if let Err(error) = load(Some(&kmi)) {
        warn!("Failed to auto-load SUSFS guard LKM for KMI {kmi}: {error:#}");
    }
    Ok(())
}

pub fn load(kmi: Option<&str>) -> Result<()> {
    anyhow::ensure!(
        cfg!(any(target_arch = "aarch64", target_arch = "x86_64")),
        "SUSFS guard LKM is only available for arm64 and x86_64"
    );

    if let Some(version) = detected_susfs_version() {
        info!("SUSFS {version} is already available; skipping guard LKM load");
        println!("SUSFS {version} is already loaded");
        return Ok(());
    }

    let kmi = match kmi {
        Some(kmi) => kmi.to_owned(),
        None => crate::boot_patch::get_current_kmi().context(
            "Failed to detect kernel KMI; specify it explicitly with `ksud susfs load --kmi <KMI>`",
        )?,
    };

    let asset_name = format!("{kmi}_susfs_guard_lkm.ko");
    let module_data = crate::assets::get_asset_data(&asset_name)
        .with_context(|| format!("Failed to load SUSFS guard asset {asset_name}"))?;
    let params = CString::new("").expect("empty module parameters are valid");

    info!("Loading SUSFS guard LKM for KMI {kmi}");
    ksuinit::load_module(&module_data, &params).context("Failed to load SUSFS guard LKM")?;
    println!("Loaded SUSFS guard LKM for KMI {kmi}");
    Ok(())
}

pub fn unload() -> Result<()> {
    rustix::system::delete_module(c"susfs_guard_lkm", 0)
        .context("Failed to unload SUSFS guard LKM")?;
    println!("Unloaded SUSFS guard LKM");
    Ok(())
}

#[repr(C)]
struct SusfsVersion {
    version: [u8; SUSFS_MAX_VERSION_BUFSIZE],
    err: i32,
}

#[repr(C)]
struct SusfsVariant {
    variant: [u8; SUSFS_MAX_VARIANT_BUFSIZE],
    err: i32,
}

#[repr(C)]
struct SusfsEnabledFeatures {
    features: [u8; SUSFS_ENABLED_FEATURES_SIZE],
    err: i32,
}

pub fn show_version() -> Result<()> {
    let mut cmd = SusfsVersion {
        version: [0; SUSFS_MAX_VERSION_BUFSIZE],
        err: ERR_CMD_NOT_SUPPORTED,
    };

    unsafe {
        syscall(SYS_reboot, KSU_INSTALL_MAGIC1, SUSFS_MAGIC, CMD_SUSFS_SHOW_VERSION, &mut cmd as *mut _);
    }

    check_unsupported(cmd.err, CMD_SUSFS_SHOW_VERSION)?;

    if cmd.err == 0 {
        let version = unsafe { CStr::from_ptr(cmd.version.as_ptr() as *const c_char) }.to_string_lossy();
        println!("{}", version);
        Ok(())
    } else {
        Err(anyhow!("Invalid (Error: {})", cmd.err))
    }
}

pub fn show_variant() -> Result<()> {
    let mut cmd = SusfsVariant {
        variant: [0; SUSFS_MAX_VARIANT_BUFSIZE],
        err: ERR_CMD_NOT_SUPPORTED,
    };

    unsafe {
        syscall(SYS_reboot, KSU_INSTALL_MAGIC1, SUSFS_MAGIC, CMD_SUSFS_SHOW_VARIANT, &mut cmd as *mut _);
    }

    check_unsupported(cmd.err, CMD_SUSFS_SHOW_VARIANT)?;

    if cmd.err == 0 {
        let variant = unsafe { CStr::from_ptr(cmd.variant.as_ptr() as *const c_char) }.to_string_lossy();
        println!("{}", variant);
        Ok(())
    } else {
        Err(anyhow!("Invalid (Error: {})", cmd.err))
    }
}

pub fn show_features(check_only: bool) -> Result<()> {
    let mut cmd = SusfsEnabledFeatures {
        features: [0; SUSFS_ENABLED_FEATURES_SIZE],
        err: ERR_CMD_NOT_SUPPORTED,
    };

    unsafe {
        syscall(SYS_reboot, KSU_INSTALL_MAGIC1, SUSFS_MAGIC, CMD_SUSFS_SHOW_ENABLED_FEATURES, &mut cmd as *mut _);
    }

    check_unsupported(cmd.err, CMD_SUSFS_SHOW_ENABLED_FEATURES)?;

    let features_cstr = unsafe { CStr::from_ptr(cmd.features.as_ptr() as *const c_char) };
    let has_features = cmd.err == 0 && !features_cstr.to_bytes().is_empty();

    if check_only {
        if has_features {
            println!("Supported");
            Ok(())
        } else {
            Err(anyhow!("Unsupported"))
        }
    } else if has_features {
        print!("{}", features_cstr.to_string_lossy());
        Ok(())
    } else {
        Err(anyhow!("Invalid (Error: {})", cmd.err))
    }
}

fn check_unsupported(err: i32, cmd: u64) -> Result<()> {
    if err == ERR_CMD_NOT_SUPPORTED {
        return Err(anyhow!("CMD: '0x{:x}', SUSFS operation not supported, please enable it in kernel", cmd));
    }
    Ok(())
}
