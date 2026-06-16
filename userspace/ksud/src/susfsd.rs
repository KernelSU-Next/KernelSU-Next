use std::ffi::CStr;
use anyhow::{Result, anyhow};
use libc::{syscall, SYS_reboot, c_char};

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
