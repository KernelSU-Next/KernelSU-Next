package com.rifsxd.ksunext.ui.util

import com.rifsxd.ksunext.R
import com.topjohnwu.superuser.io.SuFile

fun getSELinuxStatus(): Int = SuFile("/sys/fs/selinux/enforce").run {
    when {
        !exists() -> R.string.selinux_status_disabled
        !isFile -> R.string.selinux_status_unknown
        !canRead() -> R.string.selinux_status_enforcing
        else -> when (runCatching { newInputStream() }.getOrNull()?.bufferedReader()
            ?.use { it.runCatching { readLine() }.getOrNull()?.trim()?.toIntOrNull() }) {
            1 -> R.string.selinux_status_enforcing
            0 -> R.string.selinux_status_permissive
            else -> R.string.selinux_status_unknown
        }
    }
}
