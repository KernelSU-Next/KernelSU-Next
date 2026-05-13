package com.rifsxd.ksunext.ui.screen.template

import androidx.compose.runtime.Immutable
import com.rifsxd.ksunext.data.model.TemplateInfo

@Immutable
data class TemplateUiState(
    val isRefreshing: Boolean = false,
    val offline: Boolean = false,
    val templates: List<TemplateInfo> = emptyList(),
    val templateList: List<TemplateInfo> = emptyList(),
    val error: Throwable? = null,
)

@Immutable
data class TemplateActions(
    val onBack: () -> Unit,
    val onRefresh: (Boolean) -> Unit,
    val onImport: () -> Unit,
    val onExport: () -> Unit,
    val onCreateTemplate: () -> Unit,
    val onOpenTemplate: (TemplateInfo) -> Unit,
)
