package com.rifsxd.ksunext.ui.viewmodel

import androidx.compose.runtime.Immutable
import com.rifsxd.ksunext.ui.UiMode
import com.rifsxd.ksunext.ui.theme.AppSettings

@Immutable
data class MainActivityUiState(
    val appSettings: AppSettings,
    val pageScale: Float,
    val enableBlur: Boolean,
    val enableFloatingBottomBar: Boolean,
    val enableFloatingBottomBarBlur: Boolean,
    val enableSmoothCorner: Boolean,
    val uiMode: UiMode,
)
