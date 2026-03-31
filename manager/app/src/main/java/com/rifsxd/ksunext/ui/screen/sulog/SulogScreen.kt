package com.rifsxd.ksunext.ui.screen.sulog

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.compose.dropUnlessResumed
import androidx.lifecycle.viewmodel.compose.viewModel
import com.rifsxd.ksunext.R
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.rifsxd.ksunext.ui.util.SulogEntry
import com.rifsxd.ksunext.ui.util.SulogEventFilter
import com.rifsxd.ksunext.ui.util.SulogFile
import com.rifsxd.ksunext.ui.util.toSulogDisplayName
import com.rifsxd.ksunext.ui.viewmodel.SulogViewModel

@Destination<RootGraph>
@Composable
fun SulogScreen(navigator: DestinationsNavigator) {
    val viewModel = viewModel<SulogViewModel>()
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) {
        viewModel.refreshLatest()
    }

    val state = SulogScreenState(
        isLoading = uiState.isLoading,
        isRefreshing = uiState.isRefreshing,
        sulogStatus = uiState.sulogStatus,
        isSulogEnabled = uiState.isSulogEnabled,
        searchText = uiState.searchText,
        selectedFilters = uiState.selectedFilters,
        files = uiState.files,
        selectedFilePath = uiState.selectedFilePath,
        entries = uiState.entries,
        visibleEntries = uiState.visibleEntries,
        errorMessage = uiState.errorMessage,
    )
    val actions = SulogActions(
        onBack = dropUnlessResumed { navigator.popBackStack() },
        onRefresh = viewModel::refreshLatest,
        onEnableSulog = viewModel::enableSulog,
        onCleanFile = viewModel::cleanFile,
        onSearchTextChange = viewModel::setSearchText,
        onToggleFilter = viewModel::toggleFilter,
        onSelectFile = { viewModel.refresh(it) },
    )

    SulogScreenMaterial(state, actions)
}

@Composable
fun sulogFilterLabel(filter: SulogEventFilter): String {
    return when (filter) {
        SulogEventFilter.RootExecve -> stringResource(R.string.sulog_filter_root_execve)
        SulogEventFilter.SuCompat -> stringResource(R.string.sulog_filter_sucompat)
        SulogEventFilter.IoctlGrantRoot -> stringResource(R.string.sulog_filter_ioctl_grant_root)
        SulogEventFilter.DaemonRestart -> stringResource(R.string.sulog_filter_daemon_restart)
    }
}

@Composable
fun sulogEntryTitle(entry: SulogEntry): String {
    return entry.titleRes?.let { stringResource(it) } ?: entry.title.orEmpty()
}

@Composable
fun sulogEntryDescription(entry: SulogEntry): String? {
    return entry.descriptionRes?.let { stringResource(it) } ?: entry.description
}

fun buildSulogFileSelector(
    files: List<SulogFile>,
    selectedFilePath: String?,
): SulogFileSelector {
    if (files.isEmpty()) {
        return SulogFileSelector(
            items = emptyList(),
            selectedIndex = -1,
        )
    }

    val selectedIndex = files.indexOfFirst { it.path == selectedFilePath }
        .takeIf { it >= 0 }
        ?: 0

    return SulogFileSelector(
        items = files.map { it.name.toSulogDisplayName() },
        selectedIndex = selectedIndex,
    )
}
