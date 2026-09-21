package com.rifsxd.ksunext.ui.screen.sulog

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.filled.DeleteSweep
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.compose.dropUnlessResumed
import androidx.lifecycle.viewmodel.compose.viewModel
import com.dergoogler.mmrl.ui.component.LabelItem
import com.dergoogler.mmrl.ui.component.LabelItemDefaults
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.LocalScrollState
import com.rifsxd.ksunext.ui.rememberScrollConnection
import com.rifsxd.ksunext.ui.component.SearchAppBar
import com.rifsxd.ksunext.ui.util.SulogEntry
import com.rifsxd.ksunext.ui.util.SulogEventFilter
import com.rifsxd.ksunext.ui.util.SulogEventType
import com.rifsxd.ksunext.ui.util.SulogFile
import com.rifsxd.ksunext.ui.util.toSulogDisplayName
import com.rifsxd.ksunext.ui.viewmodel.SulogViewModel
import kotlinx.coroutines.launch

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SuLogScreen(
    state: SulogScreenState,
    actions: SulogActions,
) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    val listState = rememberLazyListState()
    val bottomBarScrollState = LocalScrollState.current
    val bottomBarScrollConnection = if (bottomBarScrollState != null) {
        rememberScrollConnection(
            isScrollingDown = bottomBarScrollState.isScrollingDown,
            scrollOffset = bottomBarScrollState.scrollOffset,
            previousScrollOffset = bottomBarScrollState.previousScrollOffset,
            threshold = 30f
        )
    } else null
    val scope = rememberCoroutineScope()
    var selectedEntry by remember { mutableStateOf<SulogEntry?>(null) }
    var showFilterMenu by remember { mutableStateOf(false) }
    var showFileMenu by remember { mutableStateOf(false) }
    val fileSelector = buildSulogFileSelector(state.files, state.selectedFilePath)

    if (selectedEntry != null) {
        SulogDetailDialog(
            entry = selectedEntry!!,
            onDismiss = { selectedEntry = null },
        )
    }

    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            SearchAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.settings_sulog),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Black,
                    )
                },
                searchText = state.searchText,
                onSearchTextChange = actions.onSearchTextChange,
                onClearClick = { actions.onSearchTextChange("") },
                onBackClick = actions.onBack,
                actionsContent = {
                    IconButton(onClick = actions.onCleanFile) {
                        Icon(
                            imageVector = Icons.Filled.DeleteSweep,
                            contentDescription = stringResource(R.string.sulog_clean_title),
                        )
                    }
                },
                dropdownContent = {
                    var showFilterMenu by remember { mutableStateOf(false) }

                    IconButton(
                        onClick = { showFilterMenu = true },
                    ) {
                        Icon(
                            imageVector = Icons.Filled.MoreVert,
                            contentDescription = stringResource(id = R.string.settings)
                        )

                        DropdownMenu(
                            expanded = showFilterMenu,
                            onDismissRequest = { showFilterMenu = false },
                        ) {
                            SulogEventFilter.entries.forEach { filter ->
                                DropdownMenuItem(
                                    text = { Text(sulogFilterLabel(filter)) },
                                    trailingIcon = {
                                        Checkbox(
                                            checked = filter in state.selectedFilters,
                                            onCheckedChange = null,
                                        )
                                    },
                                    onClick = {
                                        actions.onToggleFilter(filter)
                                    },
                                )
                            }
                        }
                    }
                },
                scrollBehavior = scrollBehavior
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
            PullToRefreshBox(
                modifier = Modifier.padding(innerPadding),
                onRefresh = actions.onRefresh,
                isRefreshing = state.isRefreshing
            ) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                ) {
                    val scrollState = LocalScrollState.current
                    val isNavBarHidden = (scrollState?.isScrollingDown?.value ?: false) || (scrollState?.isNavBarEnabled?.value == false)
                    val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + if (isNavBarHidden) 0.dp else 112.dp

                    LazyColumn(
                    state = listState,
                    modifier = Modifier
                        .fillMaxSize()
                        .let { modifier ->
                            if (bottomBarScrollConnection != null) {
                                modifier
                                    .nestedScroll(bottomBarScrollConnection)
                                    .nestedScroll(scrollBehavior.nestedScrollConnection)
                            } else {
                                modifier.nestedScroll(scrollBehavior.nestedScrollConnection)
                            }
                        },
                    verticalArrangement = Arrangement.spacedBy(16.dp),
                    contentPadding = PaddingValues(
                        start = 16.dp,
                        top = 16.dp,
                        end = 16.dp,
                        bottom = 16.dp + navBarPadding
                    )
                ) {
                    item {
                        SulogStatusSection(state, actions)
                    }

                    if (state.files.isNotEmpty()) {
                        item {
                            ListItem(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(8.dp))
                                    .clickable { showFileMenu = true },
                                headlineContent = { Text(stringResource(R.string.sulog_log_files)) },
                                supportingContent = {
                                    Text(fileSelector.items.getOrNull(fileSelector.selectedIndex) ?: "")
                                },
                                leadingContent = { Icon(Icons.AutoMirrored.Filled.List, null) },
                                trailingContent = {
                                    Box {
                                        Icon(Icons.Filled.ArrowDropDown, null)
                                        DropdownMenu(
                                            expanded = showFileMenu,
                                            onDismissRequest = { showFileMenu = false }
                                        ) {
                                            state.files.forEachIndexed { index, file ->
                                                DropdownMenuItem(
                                                    text = { Text(fileSelector.items[index]) },
                                                    onClick = {
                                                        actions.onSelectFile(file.path)
                                                        showFileMenu = false
                                                    }
                                                )
                                            }
                                        }
                                    }
                                },
                                colors = ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f))
                            )
                            Spacer(modifier = Modifier.height(16.dp))
                        }
                    }

                    sulogEntriesSection(
                        entries = state.visibleEntries,
                        errorMessage = state.errorMessage,
                        onEntryClick = { selectedEntry = it },
                    )
                }

                if (state.isLoading || state.isRefreshing) {
                    LinearProgressIndicator(
                        modifier = Modifier.fillMaxWidth().align(Alignment.TopCenter)
                    )
                }
            }
        }
    }
}

private fun LazyListScope.sulogEntriesSection(
    entries: List<SulogEntry>,
    errorMessage: String?,
    onEntryClick: (SulogEntry) -> Unit,
) {
    when {
        errorMessage != null -> item {
            SulogMessageCard(
                modifier = Modifier.fillParentMaxSize(),
                title = stringResource(R.string.sulog_failed_to_load),
                summary = errorMessage,
            )
        }

        entries.isEmpty() -> item {
            SulogMessageCard(
                modifier = Modifier.fillParentMaxSize(),
                title = stringResource(R.string.sulog_no_log_entries),
            )
        }

        else -> {
            itemsIndexed(entries, key = { index, entry -> "$index-${entry.key}" }) { index, entry ->
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clip(MaterialTheme.shapes.medium)
                        .clickable { onEntryClick(entry) },
                ) {
                    ListItem(
                        modifier = Modifier.fillMaxWidth(),
                        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                        headlineContent = {
                            Text(
                                text = sulogEntryTitle(entry),
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.SemiBold,
                            )
                        },
                        supportingContent = {
                            Column {
                                sulogEntryDescription(entry)?.let {
                                    Text(
                                        text = it,
                                        style = MaterialTheme.typography.bodySmall,
                                        maxLines = 2,
                                        overflow = TextOverflow.Ellipsis,
                                    )
                                }

                                Spacer(modifier = Modifier.height(4.dp))

                                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                                    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                        entry.timestampText?.let {
                                            LabelItem(
                                                text = it,
                                                style = LabelItemDefaults.style.copy(
                                                    containerColor = MaterialTheme.colorScheme.surfaceVariant,
                                                    contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                                                )
                                            )
                                        }
                                        sulogEntryStatus(entry)?.let { status ->
                                            val isSuccess = status == "Success"
                                            LabelItem(
                                                text = status,
                                                style = LabelItemDefaults.style.copy(
                                                    containerColor = if (isSuccess)
                                                        MaterialTheme.colorScheme.tertiaryContainer
                                                    else
                                                        MaterialTheme.colorScheme.errorContainer,
                                                    contentColor = if (isSuccess)
                                                        MaterialTheme.colorScheme.onTertiaryContainer
                                                    else
                                                        MaterialTheme.colorScheme.onErrorContainer,
                                                )
                                            )
                                        }
                                    }
                                    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                        sulogEntrySummaryTags(entry).forEach { tag ->
                                            LabelItem(
                                                text = tag,
                                                style = LabelItemDefaults.style.copy(
                                                    containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                                    contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
                                                )
                                            )
                                        }
                                    }
                                }
                            }
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun SulogStatusSection(
    state: SulogScreenState,
    actions: SulogActions,
) {
    when (state.sulogStatus) {
        "unsupported" -> {
            WarningCard(text = stringResource(R.string.sulog_unsupported_title))
        }

        "managed" -> {
            WarningCard(text = stringResource(R.string.feature_status_managed_summary))
        }

        "supported" if !state.isSulogEnabled -> {
            WarningCard(
                text = stringResource(R.string.sulog_disabled_title),
                action = {
                    Button(
                        onClick = actions.onEnableSulog,
                        colors = ButtonDefaults.buttonColors(
                            containerColor = MaterialTheme.colorScheme.error,
                            contentColor = MaterialTheme.colorScheme.onError,
                        ),
                    ) {
                        Text(stringResource(R.string.sulog_enable_action))
                    }
                },
            )
        }

        else -> Unit
    }
}

@Composable
private fun SulogMessageCard(
    modifier: Modifier,
    title: String,
    summary: String? = null,
) {
    Box(
        modifier = modifier,
        contentAlignment = Alignment.Center
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title, color = MaterialTheme.colorScheme.outline)
            if (summary != null) {
                Text(
                    summary,
                    color = MaterialTheme.colorScheme.outline,
                    style = MaterialTheme.typography.bodySmall,
                    maxLines = 3,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
    }
}

@Composable
private fun WarningCard(
    text: String,
    action: (@Composable () -> Unit)? = null,
) {
    Card(
        modifier = Modifier.fillMaxWidth().padding(bottom = 16.dp),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 12.dp, horizontal = 16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = text,
                style = MaterialTheme.typography.bodyLarge,
                modifier = Modifier.weight(1f),
                color = MaterialTheme.colorScheme.onErrorContainer
            )
            action?.invoke()
        }
    }
}

@Composable
private fun SulogDetailDialog(
    entry: SulogEntry,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(sulogEntryTitle(entry)) },
        text = {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState())
            ) {
                SelectionContainer {
                    Text(
                        text = sulogEntryDetailText(entry),
                        fontFamily = FontFamily.Monospace,
                        fontSize = 12.sp
                    )
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(stringResource(android.R.string.ok))
            }
        },
    )
}

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
    val scrollStateOuter = LocalScrollState.current
    val hapticOuter = androidx.compose.ui.platform.LocalHapticFeedback.current

    val actions = SulogActions(
        onBack = dropUnlessResumed { navigator.popBackStack() },
        onRefresh = {
            if (scrollStateOuter?.isHapticsEnabled?.value == true) {
                hapticOuter.performHapticFeedback(androidx.compose.ui.hapticfeedback.HapticFeedbackType.LongPress)
            }
            viewModel.refreshLatest(refreshing = true)
        },
        onEnableSulog = viewModel::enableSulog,
        onCleanFile = viewModel::cleanFile,
        onSearchTextChange = viewModel::setSearchText,
        onToggleFilter = viewModel::toggleFilter,
        onSelectFile = { viewModel.refresh(it) },
    )

    SuLogScreen(state, actions)
}

@Composable
fun sulogFilterLabel(filter: SulogEventFilter): String {
    return when (filter) {
        SulogEventFilter.RootExecve -> stringResource(R.string.sulog_filter_root_execve)
        SulogEventFilter.SuCompat -> stringResource(R.string.sulog_filter_sucompat)
        SulogEventFilter.IoctlGrantRoot -> stringResource(R.string.sulog_filter_ioctl_grant_root)
        SulogEventFilter.DaemonEvent -> stringResource(R.string.sulog_filter_daemon_restart)
    }
}

@Composable
fun sulogEntryTitle(entry: SulogEntry): String {
    return when (entry.eventType) {
        SulogEventType.RootExecve -> entry.fields["comm"] ?: stringResource(R.string.sulog_filter_root_execve)
        SulogEventType.SuCompat -> stringResource(R.string.sulog_filter_sucompat)
        SulogEventType.IoctlGrantRoot -> stringResource(R.string.sulog_filter_ioctl_grant_root)
        SulogEventType.DaemonEvent -> stringResource(R.string.sulog_filter_daemon_restart)
        SulogEventType.Dropped -> stringResource(R.string.sulog_dropped)
        SulogEventType.Unknown -> entry.fields["type"]?.replace('_', ' ')?.replaceFirstChar(Char::uppercase) ?: stringResource(R.string.sulog_unknown)
    }
}

@Composable
fun sulogEntryDescription(entry: SulogEntry): String? {
    return when (entry.eventType) {
        SulogEventType.DaemonEvent -> entry.fields["boot_id"]?.let { stringResource(R.string.sulog_boot_id, it) }
        SulogEventType.Dropped -> entry.fields["ts_ns"]?.let { stringResource(R.string.sulog_timestamp, it) }
        else -> entry.fields["argv"] ?: entry.fields["file"]
    }
}

@Composable
fun sulogEntrySummaryTags(entry: SulogEntry): List<String> {
    val comm = entry.fields["comm"]
    val pid = entry.fields["pid"]
    val uid = entry.fields["uid"]
    return when (entry.eventType) {
        SulogEventType.DaemonEvent -> listOfNotNull(entry.fields["restart"]?.let { stringResource(R.string.sulog_restart_number, it) } ?: stringResource(R.string.sulog_daemon_restarted))
        SulogEventType.Dropped -> listOfNotNull(entry.fields["dropped"]?.let { stringResource(R.string.sulog_lost_count, it) })
        else -> listOfNotNull(comm?.takeIf { it.isNotBlank() }, pid?.let { stringResource(R.string.sulog_pid, it) }, uid?.let { stringResource(R.string.sulog_uid, it) })
    }
}

fun sulogEntryDetailText(entry: SulogEntry) = buildAnnotatedString {
    entry.fields.entries.forEachIndexed { index, (key, value) ->
        if (index > 0) append('\n')
        withStyle(SpanStyle(fontWeight = FontWeight.Bold)) {
            append("$key: ")
        }
        append(value)
    }
}

@Composable
fun sulogEntryStatus(entry: SulogEntry): String? {
    return entry.fields["retval"]?.toIntOrNull()?.let { retval ->
        if (retval == 0) {
            stringResource(R.string.sulog_status_success)
        } else {
            stringResource(R.string.sulog_status_exit, retval)
        }
    }
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

data class SulogScreenState(
    val isLoading: Boolean = true,
    val isRefreshing: Boolean = false,
    val sulogStatus: String = "",
    val isSulogEnabled: Boolean = false,
    val searchText: String = "",
    val selectedFilters: Set<SulogEventFilter> = emptySet(),
    val files: List<SulogFile> = emptyList(),
    val selectedFilePath: String? = null,
    val entries: List<SulogEntry> = emptyList(),
    val visibleEntries: List<SulogEntry> = emptyList(),
    val errorMessage: String? = null,
)

data class SulogActions(
    val onBack: () -> Unit,
    val onRefresh: () -> Unit,
    val onEnableSulog: () -> Unit,
    val onCleanFile: () -> Unit,
    val onSearchTextChange: (String) -> Unit,
    val onToggleFilter: (SulogEventFilter) -> Unit,
    val onSelectFile: (String) -> Unit,
)

data class SulogFileSelector(
    val items: List<String>,
    val selectedIndex: Int,
)
