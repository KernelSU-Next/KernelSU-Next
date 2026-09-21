package com.rifsxd.ksunext.ui.screen

import android.content.ClipData
import android.widget.Toast
import androidx.compose.animation.*
import androidx.compose.animation.core.tween
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.ExperimentalMaterialApi
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ImportExport
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material3.*
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.ClipEntry
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import com.rifsxd.ksunext.ui.LocalScrollState
import com.rifsxd.ksunext.ui.rememberScrollConnection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import androidx.lifecycle.viewmodel.compose.viewModel
import com.dergoogler.mmrl.ui.component.LabelItem
import com.dergoogler.mmrl.ui.component.LabelItemDefaults
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.TemplateEditorScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.result.ResultRecipient
import com.ramcosta.composedestinations.result.getOr
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.viewmodel.TemplateViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * @author weishu
 * @date 2023/10/20.
 */

@OptIn(ExperimentalMaterialApi::class, ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun AppProfileTemplateScreen(
    navigator: DestinationsNavigator,
    resultRecipient: ResultRecipient<TemplateEditorScreenDestination, Boolean>
) {
    val viewModel = viewModel<TemplateViewModel>()
    val scope = rememberCoroutineScope()
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    // Bottom bar scroll tracking
    val bottomBarScrollState = LocalScrollState.current
    val bottomBarScrollConnection = if (bottomBarScrollState != null) {
        rememberScrollConnection(
            isScrollingDown = bottomBarScrollState.isScrollingDown,
            scrollOffset = bottomBarScrollState.scrollOffset,
            previousScrollOffset = bottomBarScrollState.previousScrollOffset,
            threshold = 30f
        )
    } else null

    LaunchedEffect(Unit) {
        if (viewModel.templateList.isEmpty()) {
            viewModel.fetchTemplates()
        }
    }

    // handle result from TemplateEditorScreen, refresh if needed
    resultRecipient.onNavResult { result ->
        if (result.getOr { false }) {
            scope.launch { viewModel.fetchTemplates() }
        }
    }

    val listState = rememberLazyListState()

    Scaffold(
        topBar = {
            val clipboard = LocalClipboard.current
            val context = LocalContext.current
            val showToast = fun(msg: String) {
                scope.launch(Dispatchers.Main) {
                    Toast.makeText(context, msg, Toast.LENGTH_SHORT).show()
                }
            }
                    TopBar(
                        onBack = dropUnlessResumed { navigator.popBackStack() },
                
                        onImport = {
                            scope.launch {
                                clipboard.getClipEntry()?.clipData?.getItemAt(0)?.text?.toString()?.let {
                                    if (it.isEmpty()) {
                                        showToast(context.getString(R.string.app_profile_template_import_empty))
                                        return@let
                                    }
                                    viewModel.importTemplates(
                                        it, {
                                            showToast(context.getString(R.string.app_profile_template_import_success))
                                            viewModel.fetchTemplates(false)
                                        },
                                        showToast
                                    )
                                }
                            }
                        },
                        onExport = {
                            scope.launch {
                                viewModel.exportTemplates(
                                    {
                                        showToast(context.getString(R.string.app_profile_template_export_empty))
                                    },
                                    {
                                        scope.launch { clipboard.setClipEntry(ClipEntry(ClipData.newPlainText("template", it))) }
                                    }
                                )


                            }
                        },
                        onCreate = {
                            navigator.navigate(
                                TemplateEditorScreenDestination(
                                    TemplateViewModel.TemplateInfo(),
                                    false
                                )
                            )
                        },
                        scrollBehavior = scrollBehavior
                    )
        },
        floatingActionButton = {},
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { innerPadding ->
        PullToRefreshBox(
            modifier = Modifier.padding(innerPadding),
            isRefreshing = viewModel.isRefreshing,
            onRefresh = {
                scope.launch { viewModel.fetchTemplates(true) }
            }
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
                contentPadding = PaddingValues(
                    bottom = 16.dp + navBarPadding
                )
            ) {
                items(viewModel.templateList, key = { it.id }) { app ->
                    TemplateItem(navigator, app)
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TemplateItem(
    navigator: DestinationsNavigator,
    template: TemplateViewModel.TemplateInfo
) {
    ListItem(
        modifier = Modifier
            .clickable {
                navigator.navigate(TemplateEditorScreenDestination(template, !template.local))
            },
        headlineContent = { Text(
            text = template.name,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
        ) },
        supportingContent = {
            Column {
                Text(
                    text = "${template.id}${if (template.author.isEmpty()) "" else "@${template.author}"}",
                    style = MaterialTheme.typography.bodySmall,
                    fontSize = MaterialTheme.typography.bodySmall.fontSize,
                )
                Text(template.description)

                Spacer(modifier = Modifier.height(4.dp))

                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(4.dp)
                ) {
                    LabelItem(
                        text = "UID: ${template.uid}"
                    )
                    LabelItem(
                        text = "GID: ${template.gid}",
                        style = LabelItemDefaults.style.copy(
                            containerColor = MaterialTheme.colorScheme.secondaryContainer,
                            contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                    )
                    LabelItem(
                        text = template.context,
                        style = LabelItemDefaults.style.copy(
                            containerColor = MaterialTheme.colorScheme.secondaryContainer,
                            contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                        )
                    )
                    if (template.local) {
                        LabelItem(
                            text = "local",
                            style = LabelItemDefaults.style.copy(
                                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                contentColor = MaterialTheme.colorScheme.onSecondaryContainer 
                            )
                        )
                    } else {
                        LabelItem(
                            text = "remote",
                            style = LabelItemDefaults.style.copy(
                                containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                                contentColor = MaterialTheme.colorScheme.onTertiaryContainer
                            )
                        )
                    }
                }
            }
        },
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    onBack: () -> Unit,
    onImport: () -> Unit = {},
    onExport: () -> Unit = {},
    onCreate: () -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.settings_profile_template),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Black,
            )
        },
        navigationIcon = {
            IconButton(
                onClick = onBack
            ) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null) }
        },
        actions = {
            IconButton(onClick = onCreate) {
                Icon(
                    Icons.Filled.Add,
                    contentDescription = stringResource(id = R.string.app_profile_template_create)
                )
            }

            var showDropdown by remember { mutableStateOf(false) }
            IconButton(onClick = {
                showDropdown = true
            }) {
                Icon(
                    imageVector = Icons.Filled.ImportExport,
                    contentDescription = stringResource(id = R.string.app_profile_import_export)
                )

                DropdownMenu(expanded = showDropdown, onDismissRequest = {
                    showDropdown = false
                }) {
                    DropdownMenuItem(text = {
                        Text(stringResource(id = R.string.app_profile_import_from_clipboard))
                    }, onClick = {
                        onImport()
                        showDropdown = false
                    })
                    DropdownMenuItem(text = {
                        Text(stringResource(id = R.string.app_profile_export_to_clipboard))
                    }, onClick = {
                        onExport()
                        showDropdown = false
                    })
                }
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}