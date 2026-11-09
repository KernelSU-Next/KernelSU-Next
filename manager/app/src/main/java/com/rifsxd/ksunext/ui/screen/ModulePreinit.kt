package com.rifsxd.ksunext.ui.screen

import androidx.annotation.StringRes
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.viewmodel.ModuleViewModel

/** Message for a module whose preinit actions will not run on the next boot, null otherwise. */
@StringRes
fun ModuleViewModel.ModuleInfo.preinitWarning(): Int? = when (preinit) {
    "no_space" -> R.string.module_preinit_no_space
    "error" -> R.string.module_preinit_error
    "bootloop" -> R.string.module_preinit_bootloop
    "unsupported" -> R.string.module_preinit_unsupported
    else -> null
}
