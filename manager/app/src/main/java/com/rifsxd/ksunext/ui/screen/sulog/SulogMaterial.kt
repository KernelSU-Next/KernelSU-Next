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
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Article
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.launch
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.component.SearchAppBar
import com.rifsxd.ksunext.ui.util.SulogEntry
import com.rifsxd.ksunext.ui.util.SulogEventFilter

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SulogScreenMaterial(
    state: SulogScreenState,
    actions: SulogActions,
) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    val listState = rememberLazyListState()
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
                    IconButton(onClick = { showFilterMenu = true }) {
                        Icon(
                            imageVector = Icons.Filled.FilterList,
                            contentDescription = stringResource(R.string.sulog_filter_title),
                        )
                    }
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
                },
                scrollBehavior = scrollBehavior
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
        ) {
            LazyColumn(
                state = listState,
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
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
                title = "No log entries",
            )
        }

        else -> {
            itemsIndexed(entries, key = { _, entry -> entry.key }) { index, entry ->
                ElevatedCard(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                        .clickable { onEntryClick(entry) },
                ) {
                    ListItem(
                        headlineContent = { Text(sulogEntryTitle(entry), fontWeight = FontWeight.Bold) },
                        supportingContent = {
                            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                sulogEntryDescription(entry)?.let {
                                    Text(
                                        it,
                                        style = MaterialTheme.typography.bodySmall,
                                        maxLines = 2,
                                        overflow = TextOverflow.Ellipsis
                                    )
                                }
                                entry.timestampText?.let {
                                    Text(
                                        it,
                                        style = MaterialTheme.typography.labelMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    )
                                }
                                Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                                    entry.summaryTags.forEach { tag ->
                                        StatusTag(label = tag)
                                    }
                                }
                            }
                        },
                        trailingContent = {
                            entry.status?.let { Text(it, style = MaterialTheme.typography.labelLarge) }
                        }
                    )
                }
            }
        }
    }
}

@Composable
private fun StatusTag(label: String) {
    Box(
        modifier = Modifier
            .background(
                color = MaterialTheme.colorScheme.secondaryContainer,
                shape = RoundedCornerShape(4.dp)
            )
            .padding(horizontal = 6.dp, vertical = 2.dp)
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSecondaryContainer,
        )
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
                        text = entry.detailText,
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
