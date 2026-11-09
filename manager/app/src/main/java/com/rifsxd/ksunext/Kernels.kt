package com.rifsxd.ksunext

import android.system.Os

/**
 * @author weishu
 * @date 2022/12/10.
 */

data class KernelVersion(val major: Int, val patchLevel: Int, val subLevel: Int) {
    override fun toString(): String {
        return "$major.$patchLevel.$subLevel"
    }

    fun isGKI(): Boolean {

        // kernel 6.x
        if (major > 5) {
            return true
        }

        // kernel 5.10.x
        if (major == 5) {
            return patchLevel >= 10
        }

        return false
    }

    fun isULegacy(): Boolean {
        return major == 3
    }

    fun isLegacy(): Boolean {
        return major == 4 && patchLevel in 1..18
    }

    fun isGKI1(): Boolean {
        return (major == 4 && patchLevel >= 19) || (major == 5 && patchLevel < 10)
    }

}

fun KernelVersion.getKernelType(): String {
    return when {
        isGKI() -> "GKI2"
        isULegacy() -> "U-LEGACY"
        isLegacy() -> "LEGACY"
        isGKI1() -> "GKI1"
        else -> "UNKNOWN"
    }
}

fun KernelVersion.isKernel510(): Boolean {
    return major == 5 && patchLevel == 10
}

fun parseKernelVersion(version: String): KernelVersion {
    val find = "(\\d+)\\.(\\d+)\\.(\\d+)".toRegex().find(version)
    if (find != null) {
        val major = find.groupValues[1].toIntOrNull()
        val patchLevel = find.groupValues[2].toIntOrNull()
        val subLevel = find.groupValues[3].toIntOrNull()
        if (major != null && patchLevel != null && subLevel != null) {
            return KernelVersion(major, patchLevel, subLevel)
        }
    }
    return KernelVersion(-1, -1, -1)
}

fun getKernelVersion(): KernelVersion {
    Os.uname().release.let {
        return parseKernelVersion(it)
    }
}