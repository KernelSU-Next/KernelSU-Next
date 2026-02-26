package com.rifsxd.ksunext.ui.screen


import android.app.Activity.RESULT_OK
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.*
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.Wysiwyg
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.outlined.Delete
import androidx.compose.material.icons.outlined.AttachMoney
import androidx.compose.material.icons.outlined.Download
import androidx.compose.material.icons.outlined.Restore
import androidx.compose.material.icons.outlined.Terminal
import androidx.compose.material3.*
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import com.rifsxd.ksunext.ui.LocalScrollState
import com.rifsxd.ksunext.ui.rememberScrollConnection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.lifecycle.compose.dropUnlessResumed
import androidx.lifecycle.viewmodel.compose.viewModel
import coil.compose.AsyncImage
import coil.request.ImageRequest
import com.dergoogler.mmrl.ui.component.LabelItem
import com.dergoogler.mmrl.ui.component.LabelItemDefaults
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.ModuleRepoScreenDestination
import com.ramcosta.composedestinations.generated.destinations.ExecuteModuleActionScreenDestination
import com.ramcosta.composedestinations.generated.destinations.FlashScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.navigation.EmptyDestinationsNavigator
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.ui.component.ConfirmResult
import com.rifsxd.ksunext.ui.component.SearchAppBar
import com.rifsxd.ksunext.ui.component.rememberConfirmDialog
import com.rifsxd.ksunext.ui.component.rememberLoadingDialog
import com.rifsxd.ksunext.ui.component.ShortcutDialog
import com.rifsxd.ksunext.ui.util.module.Shortcut
import com.rifsxd.ksunext.ui.util.*
import com.rifsxd.ksunext.ui.viewmodel.ModuleViewModel
import com.rifsxd.ksunext.ui.webui.WebUIActivity
import com.topjohnwu.superuser.io.SuFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun ModuleScreen(navigator: DestinationsNavigator) {
    val viewModel = viewModel<ModuleViewModel>()
    val context = LocalContext.current
    val snackBarHost = LocalSnackbarHost.current
    val scope = rememberCoroutineScope()
    val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    LaunchedEffect(Unit) {
        viewModel.sortAToZ = prefs.getBoolean("module_sort_a_to_z", true)
        viewModel.sortZToA = prefs.getBoolean("module_sort_z_to_a", false)
        viewModel.sortSizeLowToHigh = prefs.getBoolean("module_sort_size_low_to_high", false)
        viewModel.sortSizeHighToLow = prefs.getBoolean("module_sort_size_high_to_low", false)
        viewModel.sortEnabledFirst = prefs.getBoolean("module_sort_enabled_first", false)
        viewModel.sortActionFirst = prefs.getBoolean("module_sort_action_first", false)
        viewModel.sortWebUiFirst = prefs.getBoolean("module_sort_webui_first", false)

        if (viewModel.moduleList.isEmpty() || viewModel.isNeedRefresh) {
            viewModel.fetchModuleList()
        }
    }

    val isSafeMode = Natives.isSafeMode
    val hasMagisk = hasMagisk()

    val hideInstallButton = isSafeMode || hasMagisk

    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())

    var zipUri by remember { mutableStateOf<Uri?>(null) }
    var zipUris by remember { mutableStateOf<List<Uri>>(emptyList()) }

    var showConfirmDialog by remember { mutableStateOf(false) }

    val webUILauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { viewModel.fetchModuleList() }

    val selectZipLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode != RESULT_OK) {
            return@rememberLauncherForActivityResult
        }
        val data = result.data ?: return@rememberLauncherForActivityResult
        val clipData = data.clipData

        val uris = mutableListOf<Uri>()
        if (clipData != null) {
            for (i in 0 until clipData.itemCount) {
                clipData.getItemAt(i)?.uri?.let { uris.add(it) }
            }
        } else {
            data.data?.let { uris.add(it) }
        }

        if (uris.isEmpty()) return@rememberLauncherForActivityResult

        viewModel.updateZipUris(uris)

        navigator.navigate(FlashScreenDestination(FlashIt.FlashModules(uris)))
        viewModel.clearZipUris()
        viewModel.markNeedRefresh()
    }

    val listState = rememberLazyListState()

    val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + 112.dp

    Scaffold(
        topBar = {
            SearchAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.module),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Black,
                    )
                },
                searchText = viewModel.search,
                onSearchTextChange = { viewModel.search = it },
                onClearClick = { viewModel.search = "" },
                actionsContent = {
                    IconButton(
                        onClick = { navigator.navigate(ModuleRepoScreenDestination) }
                    ) {
                        Icon(
                            imageVector = Icons.Filled.Cloud,
                            contentDescription = stringResource(id = R.string.module_repo_screen)
                        )
                    }

                    if (!hideInstallButton) {
                        IconButton(
                            onClick = {
                                val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
                                    type = "application/zip"
                                    putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
                                }
                                selectZipLauncher.launch(intent)
                            }
                        ) {
                            Icon(
                                imageVector = Icons.Filled.Add,
                                contentDescription = stringResource(id = R.string.module_install)
                            )
                        }
                    }
                },
                dropdownContent = {
                    var showDropdown by remember { mutableStateOf(false) }
                    IconButton(
                        onClick = { showDropdown = true },
                    ) {
                        Icon(
                            imageVector = Icons.Filled.MoreVert,
                            contentDescription = stringResource(id = R.string.settings)
                        )
                        DropdownMenu(
                            expanded = showDropdown,
                            onDismissRequest = {
                                showDropdown = false
                            }
                        ) {
                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_a_to_z))
                                },
                                trailingIcon = {
                                    Checkbox(checked = viewModel.sortAToZ, onCheckedChange = null)
                                },
                                onClick = {
                                    viewModel.sortAToZ = !viewModel.sortAToZ
                                    viewModel.sortZToA = false
                                    viewModel.sortSizeLowToHigh = false
                                    viewModel.sortSizeHighToLow = false
                                    viewModel.sortEnabledFirst = false
                                    viewModel.sortActionFirst = false
                                    viewModel.sortWebUiFirst = false
                                    prefs.edit {
                                        putBoolean("module_sort_a_to_z", viewModel.sortAToZ)
                                        putBoolean("module_sort_z_to_a", false)
                                        putBoolean("module_sort_size_low_to_high", false)
                                        putBoolean("module_sort_size_high_to_low", false)
                                        putBoolean("module_sort_enabled_first", false)
                                        putBoolean("module_sort_action_first", false)
                                        putBoolean("module_sort_webui_first", false)
                                    }
                                    scope.launch {
                                        viewModel.fetchModuleList()
                                    }
                                }
                            )

                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_z_to_a))
                                },
                                trailingIcon = {
                                    Checkbox(checked = viewModel.sortZToA, onCheckedChange = null)
                                },
                                onClick = {
                                    viewModel.sortZToA = !viewModel.sortZToA
                                    viewModel.sortAToZ = false
                                    viewModel.sortSizeLowToHigh = false
                                    viewModel.sortSizeHighToLow = false
                                    viewModel.sortEnabledFirst = false
                                    viewModel.sortActionFirst = false
                                    viewModel.sortWebUiFirst = false
                                    prefs.edit {
                                        putBoolean("module_sort_z_to_a", viewModel.sortZToA)
                                        putBoolean("module_sort_a_to_z", false)
                                        putBoolean("module_sort_size_low_to_high", false)
                                        putBoolean("module_sort_size_high_to_low", false)
                                        putBoolean("module_sort_enabled_first", false)
                                        putBoolean("module_sort_action_first", false)
                                        putBoolean("module_sort_webui_first", false)
                                    }
                                    scope.launch {
                                        viewModel.fetchModuleList()
                                    }
                                }
                            )

                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_size_low_to_high))
                                },
                                trailingIcon = {
                                    Checkbox(checked = viewModel.sortSizeLowToHigh, onCheckedChange = null)
                                },
                                onClick = {
                                    viewModel.sortSizeLowToHigh = !viewModel.sortSizeLowToHigh
                                    viewModel.sortAToZ = false
                                    viewModel.sortZToA = false
                                    viewModel.sortSizeHighToLow = false
                                    viewModel.sortEnabledFirst = false
                                    viewModel.sortActionFirst = false
                                    viewModel.sortWebUiFirst = false
                                    prefs.edit {
                                        putBoolean("module_sort_size_low_to_high", viewModel.sortSizeLowToHigh)
                                        putBoolean("module_sort_a_to_z", false)
                                        putBoolean("module_sort_z_to_a", false)
                                        putBoolean("module_sort_size_high_to_low", false)
                                        putBoolean("module_sort_enabled_first", false)
                                        putBoolean("module_sort_action_first", false)
                                        putBoolean("module_sort_webui_first", false)
                                    }
                                    scope.launch {
                                        viewModel.fetchModuleList()
                                    }
                                }
                            )

                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_size_high_to_low))
                                },
                                trailingIcon = {
                                    Checkbox(checked = viewModel.sortSizeHighToLow, onCheckedChange = null)
                                },
                                onClick = {
                                    viewModel.sortSizeHighToLow = !viewModel.sortSizeHighToLow
                                    viewModel.sortAToZ = false
                                    viewModel.sortZToA = false
                                    viewModel.sortSizeLowToHigh = false
                                    viewModel.sortEnabledFirst = false
                                    viewModel.sortActionFirst = false
                                    viewModel.sortWebUiFirst = false
                                    prefs.edit {
                                        putBoolean("module_sort_size_high_to_low", viewModel.sortSizeHighToLow)
                                        putBoolean("module_sort_a_to_z", false)
                                        putBoolean("module_sort_z_to_a", false)
                                        putBoolean("module_sort_size_low_to_high", false)
                                        putBoolean("module_sort_enabled_first", false)
                                        putBoolean("module_sort_action_first", false)
                                        putBoolean("module_sort_webui_first", false)
                                    }
                                    scope.launch {
                                        viewModel.fetchModuleList()
                                    }
                                }
                            )
                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_enabled_first))
                                },
                                trailingIcon = {
                                    Checkbox(checked = viewModel.sortEnabledFirst, onCheckedChange = null)
                                },
                                onClick = {
                                    viewModel.sortEnabledFirst = !viewModel.sortEnabledFirst
                                    viewModel.sortAToZ = false
                                    viewModel.sortZToA = false
                                    viewModel.sortSizeLowToHigh = false
                                    viewModel.sortSizeHighToLow = false
                                    viewModel.sortActionFirst = false
                                    viewModel.sortWebUiFirst = false
                                    prefs.edit {
                                        putBoolean("module_sort_enabled_first", viewModel.sortEnabledFirst)
                                        putBoolean("module_sort_a_to_z", false)
                                        putBoolean("module_sort_z_to_a", false)
                                        putBoolean("module_sort_size_low_to_high", false)
                                        putBoolean("module_sort_size_high_to_low", false)
                                        putBoolean("module_sort_action_first", false)
                                        putBoolean("module_sort_webui_first", false)
                                    }
                                    scope.launch {
                                        viewModel.fetchModuleList()
                                    }
                                }
                            )
                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_action_first))
                                },
                                trailingIcon = {
                                    Checkbox(checked = viewModel.sortActionFirst, onCheckedChange = null)
                                },
                                onClick = {
                                    viewModel.sortActionFirst = !viewModel.sortActionFirst
                                    viewModel.sortAToZ = false
                                    viewModel.sortZToA = false
                                    viewModel.sortSizeLowToHigh = false
                                    viewModel.sortSizeHighToLow = false
                                    viewModel.sortEnabledFirst = false
                                    viewModel.sortWebUiFirst = false
                                    prefs.edit {
                                        putBoolean("module_sort_action_first", viewModel.sortActionFirst)
                                        putBoolean("module_sort_a_to_z", false)
                                        putBoolean("module_sort_z_to_a", false)
                                        putBoolean("module_sort_size_low_to_high", false)
                                        putBoolean("module_sort_size_high_to_low", false)
                                        putBoolean("module_sort_enabled_first", false)
                                        putBoolean("module_sort_webui_first", false)
                                    }
                                    scope.launch {
                                        viewModel.fetchModuleList()
                                    }
                                }
                            )
                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_webui_first))
                                },
                                trailingIcon = {
                                    Checkbox(checked = viewModel.sortWebUiFirst, onCheckedChange = null)
                                },
                                onClick = {
                                    viewModel.sortWebUiFirst = !viewModel.sortWebUiFirst
                                    viewModel.sortAToZ = false
                                    viewModel.sortZToA = false
                                    viewModel.sortSizeLowToHigh = false
                                    viewModel.sortSizeHighToLow = false
                                    viewModel.sortEnabledFirst = false
                                    viewModel.sortActionFirst = false
                                    prefs.edit {
                                        putBoolean("module_sort_webui_first", viewModel.sortWebUiFirst)
                                        putBoolean("module_sort_a_to_z", false)
                                        putBoolean("module_sort_z_to_a", false)
                                        putBoolean("module_sort_size_low_to_high", false)
                                        putBoolean("module_sort_size_high_to_low", false)
                                        putBoolean("module_sort_enabled_first", false)
                                        putBoolean("module_sort_action_first", false)
                                    }
                                    scope.launch {
                                        viewModel.fetchModuleList()
                                    }
                                }
                            )
                        }
                    }
                },
                scrollBehavior = scrollBehavior
            )
        },
        floatingActionButton = {},
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        snackbarHost = {
            SnackbarHost(
                hostState = snackBarHost,
                modifier = Modifier.padding(bottom = navBarPadding)
            )
        }
    ) { innerPadding ->

        when {
            hasMagisk -> {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(24.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Text(
                        stringResource(R.string.module_magisk_conflict),
                        textAlign = TextAlign.Center,
                    )
                }
            }

            else -> {
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

                ModuleList(
                    navigator,
                    viewModel = viewModel,
                    modifier = Modifier
                        .let { modifier ->
                            if (bottomBarScrollConnection != null) {
                                modifier
                                    .nestedScroll(bottomBarScrollConnection)
                                    .nestedScroll(scrollBehavior.nestedScrollConnection)
                            } else {
                                modifier.nestedScroll(scrollBehavior.nestedScrollConnection)
                            }
                        },
                    boxModifier = Modifier.padding(innerPadding),
                    onInstallModule = {
                        navigator.navigate(FlashScreenDestination(FlashIt.FlashModules(listOf(it))))
                    },
                    onClickModule = { id, name, hasWebUi ->
                        if (hasWebUi) {
                            webUILauncher.launch(
                                Intent(context, WebUIActivity::class.java)
                                    .setData("kernelsu://webui/$id".toUri())
                                    .putExtra("id", id)
                                    .putExtra("name", name)
                            )
                        }
                    },
                    context = context,
                    snackBarHost = snackBarHost,
                    listState = listState
                )
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ModuleList(
    navigator: DestinationsNavigator,
    viewModel: ModuleViewModel,
    modifier: Modifier = Modifier,
    boxModifier: Modifier = Modifier,
    onInstallModule: (Uri) -> Unit,
    onClickModule: (id: String, name: String, hasWebUi: Boolean) -> Unit,
    context: Context,
    snackBarHost: SnackbarHostState,
    listState: LazyListState
) {
    val failedEnable = stringResource(R.string.module_failed_to_enable)
    val failedDisable = stringResource(R.string.module_failed_to_disable)
    val failedUninstall = stringResource(R.string.module_uninstall_failed)
    val failedRestore = stringResource(R.string.module_restore_failed)
    val successUninstall = stringResource(R.string.module_uninstall_success)
    val successRestore = stringResource(R.string.module_restore_success)
    val reboot = stringResource(R.string.reboot)
    val rebootToApply = stringResource(R.string.reboot_to_apply)
    val moduleStr = stringResource(R.string.module)
    val uninstall = stringResource(R.string.uninstall)
    val uninstalled = stringResource(R.string.uninstalled)
    val restore = stringResource(R.string.restore)
    val cancel = stringResource(android.R.string.cancel)
    val moduleUninstallConfirm = stringResource(R.string.module_uninstall_confirm)
    val moduleRestoreConfirm = stringResource(R.string.module_restore_confirm)
    val updateText = stringResource(R.string.module_update)
    val updateLable = stringResource(R.string.module_update_available)
    val changelogText = stringResource(R.string.module_changelog)
    val downloadingText = stringResource(R.string.module_downloading)
    val startDownloadingText = stringResource(R.string.module_start_downloading)
    val fetchChangeLogFailed = stringResource(R.string.module_changelog_failed)

    val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    val hasShownWarning =
        rememberSaveable { mutableStateOf(prefs.getBoolean("has_shown_warning", false)) }

    val loadingDialog = rememberLoadingDialog()
    val confirmDialog = rememberConfirmDialog()

    var expandedModuleId by rememberSaveable { mutableStateOf<String?>(null) }

    suspend fun onModuleUpdate(
        module: ModuleViewModel.ModuleInfo,
        changelogUrl: String,
        downloadUrl: String,
        fileName: String,
    ) {
        val changelogResult = loadingDialog.withLoading {
            withContext(Dispatchers.IO) {
                runCatching {
                    ksuApp.okhttpClient.newCall(
                        okhttp3.Request.Builder().url(changelogUrl).build()
                    ).execute().body!!.string()
                }
            }
        }

        val showToast: suspend (String) -> Unit = { msg ->
            withContext(Dispatchers.Main) {
                Toast.makeText(
                    context,
                    msg,
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

        val changelog = changelogResult.getOrElse {
            showToast(fetchChangeLogFailed.format(it.message))
            return
        }.ifBlank {
            showToast(fetchChangeLogFailed.format(module.name))
            return
        }

        // changelog is not empty, show it and wait for confirm
        val confirmResult = confirmDialog.awaitConfirm(
            changelogText,
            content = changelog,
            markdown = true,
            confirm = updateText,
        )

        if (confirmResult != ConfirmResult.Confirmed) {
            return
        }

        showToast(startDownloadingText.format(module.name))

        val downloading = downloadingText.format(module.name)
        withContext(Dispatchers.IO) {
            download(
                context,
                downloadUrl,
                fileName,
                downloading,
                onDownloaded = onInstallModule,
                onDownloading = {
                    launch(Dispatchers.Main) {
                        Toast.makeText(context, downloading, Toast.LENGTH_SHORT).show()
                    }
                }
            )
        }
    }

    suspend fun onModuleUninstall(module: ModuleViewModel.ModuleInfo) {
        val confirmResult = confirmDialog.awaitConfirm(
            moduleStr,
            content = moduleUninstallConfirm.format(module.name),
            confirm = uninstall,
            dismiss = cancel
        )
        if (confirmResult != ConfirmResult.Confirmed) {
            return
        }

        val success = loadingDialog.withLoading {
            withContext(Dispatchers.IO) {
                uninstallModule(module.id)
            }
        }

        if (success) {
            viewModel.fetchModuleList()
        }
        val message = if (success) {
            successUninstall.format(module.name)
        } else {
            failedUninstall.format(module.name)
        }
        val actionLabel = if (success) {
            reboot
        } else {
            null
        }
        val result = snackBarHost.showSnackbar(
            message = message,
            actionLabel = actionLabel,
            duration = SnackbarDuration.Long
        )
        if (result == SnackbarResult.ActionPerformed) {
            reboot()
        }
    }

    suspend fun onModuleRestore(module: ModuleViewModel.ModuleInfo) {
        val confirmResult = confirmDialog.awaitConfirm(
            moduleStr,
            content = moduleRestoreConfirm.format(module.name),
            confirm = restore,
            dismiss = cancel
        )
        if (confirmResult != ConfirmResult.Confirmed) {
            return
        }

        val success = loadingDialog.withLoading {
            withContext(Dispatchers.IO) {
                restoreModule(module.id)
            }
        }

        if (success) {
            viewModel.fetchModuleList()
        }
        val message = if (success) {
            successRestore.format(module.name)
        } else {
            failedRestore.format(module.name)
        }
    }
    PullToRefreshBox(
        modifier = boxModifier,
        isRefreshing = viewModel.isRefreshing,
        onRefresh = {
            viewModel.fetchModuleList()
        }
    ) {
        val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + 112.dp

        LazyColumn(
            state = listState,
            modifier = modifier
                .fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            contentPadding = remember {
                PaddingValues(
                    start = 16.dp,
                    top = 16.dp,
                    end = 16.dp,
                    bottom = 16.dp + navBarPadding
                )
            }
        ) {
            when {
                viewModel.moduleList.isEmpty() -> {
                    item {
                        Box(
                            modifier = Modifier.fillParentMaxSize(),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(
                                stringResource(R.string.module_empty),
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                }
                else -> {
                    items(viewModel.moduleList) { module ->
                        val scope = rememberCoroutineScope()
                        val updatedModule by produceState(key1 = module.id, initialValue = Triple("", "", "")) {
                            value = withContext(Dispatchers.IO) {
                                viewModel.checkUpdate(module)
                            }
                        }

                        ModuleItem(
                            navigator = navigator,
                            module = module,
                            updateUrl = updatedModule.first,
                            onUninstall = {
                                scope.launch { onModuleUninstall(module) }
                            },
                            onRestore = {
                                scope.launch { onModuleRestore(module) }
                            },
                            onCheckChanged = {
                                scope.launch {
                                    val success = loadingDialog.withLoading {
                                        withContext(Dispatchers.IO) {
                                            toggleModule(module.id, !module.enabled)
                                        }
                                    }
                                    if (success) {
                                        viewModel.fetchModuleList()

                                        val result = snackBarHost.showSnackbar(
                                            message = rebootToApply,
                                            actionLabel = reboot,
                                            duration = SnackbarDuration.Long
                                        )
                                        if (result == SnackbarResult.ActionPerformed) {
                                            reboot()
                                        }
                                    } else {
                                        val message =
                                            if (module.enabled) failedDisable else failedEnable
                                        snackBarHost.showSnackbar(message.format(module.name))
                                    }
                                }
                            },
                            onUpdate = {
                                scope.launch {
                                    onModuleUpdate(
                                        module,
                                        updatedModule.third,
                                        updatedModule.first,
                                        "${module.name}-${updatedModule.second}.zip"
                                    )
                                    viewModel.markNeedRefresh()
                                }
                            },
                            onClick = {
                                onClickModule(it.id, it.name, it.hasWebUi)
                            },
                            expanded = expandedModuleId == module.id,
                            onExpandToggle = {
                                expandedModuleId = if (expandedModuleId == module.id) null else module.id
                            }
                        )

                        // fix last item shadow incomplete in LazyColumn
                        Spacer(Modifier.height(1.dp))
                    }
                }
            }
        }

        DownloadListener(context, onInstallModule)
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ModuleItem(
    navigator: DestinationsNavigator,
    module: ModuleViewModel.ModuleInfo,
    updateUrl: String,
    onUninstall: (ModuleViewModel.ModuleInfo) -> Unit,
    onRestore: (ModuleViewModel.ModuleInfo) -> Unit,
    onCheckChanged: (Boolean) -> Unit,
    onUpdate: (ModuleViewModel.ModuleInfo) -> Unit,
    onClick: (ModuleViewModel.ModuleInfo) -> Unit,
    expanded: Boolean,
    onExpandToggle: () -> Unit,
) {
    val viewModel = viewModel<ModuleViewModel>()
    var showMenu by remember { mutableStateOf(false) }
    var showShortcutDialog by remember { mutableStateOf(false) }
    var shortcutType by remember { mutableStateOf("") }
    var dialogInitialIcon by remember { mutableStateOf<String?>(null) }

    fun normalizeIconPath(p: String?): String? {
        if (p.isNullOrBlank()) return null

        try {
            val candidate = "/data/adb/modules/${module.id}/$p"
            val f = SuFile(candidate)
            if (f.exists()) return "su://$candidate"
        } catch (_: Exception) {
        }

        if (p.startsWith("/")) {
            try {
                val f = SuFile(p)
                if (f.exists()) return "su://$p"
            } catch (_: Exception) {
            }
            return "file://$p"
        }

        return p
    }
    
    val haptic = LocalHapticFeedback.current

    val context = LocalContext.current

    if (showShortcutDialog) {
        ShortcutDialog(
            initialName = module.name,
            initialIconUri = dialogInitialIcon,
            onDismiss = { showShortcutDialog = false },
            onConfirm = { name, iconUri ->
                showShortcutDialog = false
                if (shortcutType == "action") {
                    Shortcut.createModuleActionShortcut(context, module.id, name, iconUri)
                } else if (shortcutType == "webui") {
                    Shortcut.createModuleWebUiShortcut(context, module.id, name, iconUri)
                }
            }
        )
    }

    if (module.hasWebUi || module.hasActionScript) {
        if (showMenu) {
            Dialog(
                onDismissRequest = { showMenu = false }
            ) {
                Card(
                    modifier = Modifier.fillMaxWidth(0.95f),
                ) {
                    Column(modifier = Modifier.padding(24.dp)) {
                        Text(
                            text = stringResource(R.string.module_shortcut),
                            style = MaterialTheme.typography.headlineSmall,
                            fontWeight = FontWeight.Bold
                        )

                        Spacer(modifier = Modifier.height(14.dp))

                        // Stacked buttons: top (WebUI) and bottom (Action)
                        Column(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 8.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                        ) {
                            if (module.hasWebUi) {
                                FilledTonalButton(
                                    modifier = Modifier.fillMaxWidth(),
                                    onClick = {
                                        showMenu = false
                                        shortcutType = "webui"
                                        dialogInitialIcon = normalizeIconPath(module.webUiIconPath)
                                        showShortcutDialog = true
                                    },
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.AutoMirrored.Outlined.Wysiwyg,
                                        contentDescription = null
                                    )
                                    Text(
                                        modifier = Modifier.padding(start = 8.dp),
                                        text = stringResource(R.string.create_webui_shortcut),
                                        fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                        fontSize = MaterialTheme.typography.labelMedium.fontSize
                                    )
                                }
                            }

                            if (module.hasActionScript) {
                                FilledTonalButton(
                                    modifier = Modifier.fillMaxWidth(),
                                    onClick = {
                                        showMenu = false
                                        shortcutType = "action"
                                        dialogInitialIcon = normalizeIconPath(module.actionIconPath)
                                        showShortcutDialog = true
                                    },
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.Outlined.Terminal,
                                        contentDescription = null
                                    )
                                    Text(
                                        modifier = Modifier.padding(start = 8.dp),
                                        text = stringResource(R.string.create_action_shortcut),
                                        fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                        fontSize = MaterialTheme.typography.labelMedium.fontSize
                                    )
                                }
                            }
                        }
                        
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End
                        ) {
                            TextButton(onClick = { showMenu = false }) {
                                Text(stringResource(R.string.cancel))
                            }
                        }
                    }
                }
            }
        }
    }

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clip(MaterialTheme.shapes.medium)
            .combinedClickable(
                onClick = onExpandToggle,
                onLongClick = {
                    showMenu = true
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                }
            )
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
        ) {
            val context = LocalContext.current

            val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

            val useBanner = prefs.getBoolean("use_banner", true)

            val textDecoration = if (!module.remove) null else TextDecoration.LineThrough

            if (useBanner && module.banner.isNotEmpty()) {
                val isDark = isSystemInDarkTheme()
                val colorScheme = MaterialTheme.colorScheme
                val context = LocalContext.current
                val amoledMode = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    .getBoolean("amoled_mode", false)
                val isDynamic = colorScheme.primary != colorScheme.secondary

                val fadeColor = when {
                    amoledMode && isDark -> Color.Black
                    isDynamic -> colorScheme.surface
                    isDark -> Color(0xFF222222)
                    else -> Color.White
                }

                Box(
                    modifier = Modifier
                        .matchParentSize(),
                    contentAlignment = Alignment.Center
                ) {
                    if (module.banner.startsWith("http", true)) {
                        AsyncImage(
                            model = module.banner,
                            contentDescription = null,
                            modifier = Modifier
                                .fillMaxWidth()
                                .fillMaxHeight(),
                            contentScale = ContentScale.Crop,
                            alpha = 0.18f
                        )
                    } else {
                        val bannerData = remember(module.banner) {
                            try {
                                val file = SuFile("/data/adb/modules/${module.id}/${module.banner}")
                                return@remember file.newInputStream().use { it.readBytes() }
                            } catch (_: Exception) {
                            }

                            try {
                                val file = SuFile("/data/adb/modules_update/${module.id}/${module.banner}")
                                return@remember file.newInputStream().use { it.readBytes() }
                            } catch (_: Exception) {
                            }

                            null
                        }
                        if (bannerData != null) {
                            AsyncImage(
                                model = ImageRequest.Builder(context)
                                    .data(bannerData)
                                    .build(),
                                contentDescription = null,
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .fillMaxHeight(),
                                contentScale = ContentScale.Crop,
                                alpha = 0.18f
                            )
                        }
                    }
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .fillMaxHeight()
                            .background(
                                Brush.verticalGradient(
                                    colors = listOf(
                                        fadeColor.copy(alpha = 0.0f),
                                        fadeColor.copy(alpha = 0.8f)
                                    ),
                                    startY = 0f,
                                    endY = Float.POSITIVE_INFINITY
                                )
                            )
                    )
                }
            }
            Column {
                val interactionSource = remember { MutableInteractionSource() }

                var developerOptionsEnabled by rememberSaveable {
                    mutableStateOf(
                        prefs.getBoolean("enable_developer_options", false)
                    )
                }

                val filterZygiskModules = Natives.isZygiskEnabled() || !module.zygiskRequired
                
                val zygiskImpl by produceState(key1 = module.id, initialValue = "") {
                    value = withContext(Dispatchers.IO) { getZygiskImplementation("name") }
                }
                
                LaunchedEffect(Unit) {
                    developerOptionsEnabled = prefs.getBoolean("enable_developer_options", false)
                }

                Column(
                    modifier = Modifier
                        .padding(22.dp, 18.dp, 22.dp, 12.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        val moduleVersion = stringResource(id = R.string.module_version)
                        val moduleAuthor = stringResource(id = R.string.module_author)
                        val moduleId = stringResource(id = R.string.module_id)
                        val moduleVersionCode = stringResource(id = R.string.module_version_code)
                        val moduleUpdateJson = stringResource(id = R.string.module_update_json)
                        val moduleUpdateJsonEmpty = stringResource(id = R.string.module_update_json_empty)

                        Column(
                            modifier = Modifier.fillMaxWidth(0.8f)
                        ) {
                            Row(
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(6.dp)
                            ) {
                                LabelItem(
                                    text = formatSize(module.size)
                                )
                                if (module.remove) {
                                    LabelItem(
                                        text = stringResource(R.string.uninstalled),
                                        style = LabelItemDefaults.style.copy(
                                            containerColor = MaterialTheme.colorScheme.errorContainer,
                                            contentColor = MaterialTheme.colorScheme.onErrorContainer
                                        )
                                    )
                                }
                                if (!Natives.isZygiskEnabled() && module.zygiskRequired && !module.remove) {
                                    LabelItem(
                                        text = stringResource(R.string.zygisk_required),
                                        style = LabelItemDefaults.style.copy(
                                            containerColor = MaterialTheme.colorScheme.errorContainer,
                                            contentColor = MaterialTheme.colorScheme.onErrorContainer
                                        )
                                    )
                                }
                                if (updateUrl.isNotEmpty() && !module.remove && !module.update) {
                                    LabelItem(
                                        text = stringResource(R.string.module_update_available),
                                        style = LabelItemDefaults.style.copy(
                                            containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                                            contentColor = MaterialTheme.colorScheme.onTertiaryContainer
                                        )
                                    )
                                }
                                if (!module.remove) {
                                    if (module.update) {
                                        LabelItem(
                                            text = stringResource(R.string.module_updated),
                                            style = LabelItemDefaults.style.copy(
                                                containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                                                contentColor = MaterialTheme.colorScheme.onTertiaryContainer
                                            )
                                        )
                                    }
                                }
                                if (module.isMetaModule && !module.remove) {
                                    LabelItem(
                                        text = stringResource(R.string.meta_module),
                                        style = LabelItemDefaults.style.copy(
                                            containerColor = MaterialTheme.colorScheme.primaryContainer,
                                            contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                    )
                                }
                                if (zygiskImpl.isNotBlank() && zygiskImpl != "None" && module.name == zygiskImpl && !module.remove) {
                                    LabelItem(
                                        text = stringResource(R.string.zygisk),
                                        style = LabelItemDefaults.style.copy(
                                            containerColor = MaterialTheme.colorScheme.primaryContainer,
                                            contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                                        )
                                    )
                                }
                                if (module.enabled && !module.remove) {
                                    if (module.hasWebUi && filterZygiskModules) {
                                        LabelItem(
                                            text = stringResource(R.string.webui),
                                            style = LabelItemDefaults.style.copy(
                                                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                                contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                                            )
                                        )
                                    }
                                    if (module.hasActionScript && filterZygiskModules) {
                                        LabelItem(
                                            text = stringResource(R.string.action),
                                            style = LabelItemDefaults.style.copy(
                                                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                                contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                                            )
                                        )
                                    }
                                }
                            }

                            Spacer(modifier = Modifier.height(8.dp))

                            Text(
                                text = module.name,
                                fontSize = MaterialTheme.typography.titleMedium.fontSize,
                                fontWeight = FontWeight.SemiBold,
                                lineHeight = MaterialTheme.typography.bodySmall.lineHeight,
                                fontFamily = MaterialTheme.typography.titleMedium.fontFamily,
                                textDecoration = textDecoration
                            )

                            Text(
                                text = "$moduleVersion: ${module.version}",
                                fontSize = MaterialTheme.typography.bodySmall.fontSize,
                                lineHeight = MaterialTheme.typography.bodySmall.lineHeight,
                                fontFamily = MaterialTheme.typography.bodySmall.fontFamily,
                                textDecoration = textDecoration
                            )

                            Text(
                                text = "$moduleAuthor: ${module.author}",
                                fontSize = MaterialTheme.typography.bodySmall.fontSize,
                                lineHeight = MaterialTheme.typography.bodySmall.lineHeight,
                                fontFamily = MaterialTheme.typography.bodySmall.fontFamily,
                                textDecoration = textDecoration
                            )

                            if (developerOptionsEnabled) {

                                Text(
                                    text = "$moduleId: ${module.id}",
                                    fontSize = MaterialTheme.typography.bodySmall.fontSize,
                                    lineHeight = MaterialTheme.typography.bodySmall.lineHeight,
                                    fontFamily = MaterialTheme.typography.bodySmall.fontFamily,
                                    textDecoration = textDecoration
                                )

                                Text(
                                    text = "$moduleVersionCode: ${module.versionCode}",
                                    fontSize = MaterialTheme.typography.bodySmall.fontSize,
                                    lineHeight = MaterialTheme.typography.bodySmall.lineHeight,
                                    fontFamily = MaterialTheme.typography.bodySmall.fontFamily,
                                    textDecoration = textDecoration
                                )

                                Text(
                                    text = if (module.updateJson.isNotEmpty()) "$moduleUpdateJson: ${module.updateJson}" else "$moduleUpdateJson: $moduleUpdateJsonEmpty",
                                    fontSize = MaterialTheme.typography.bodySmall.fontSize,
                                    lineHeight = MaterialTheme.typography.bodySmall.lineHeight,
                                    fontFamily = MaterialTheme.typography.bodySmall.fontFamily,
                                    textDecoration = textDecoration
                                )
                            }
                        }

                        Spacer(modifier = Modifier.weight(1f))

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End,
                        ) {
                            Switch(
                                enabled = !module.update,
                                checked = module.enabled,
                                onCheckedChange = onCheckChanged,
                                interactionSource = if (!module.hasWebUi) interactionSource else null
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(12.dp))

                    Text(
                        text = module.description,
                        fontSize = MaterialTheme.typography.bodySmall.fontSize,
                        fontFamily = MaterialTheme.typography.bodySmall.fontFamily,
                        lineHeight = MaterialTheme.typography.bodySmall.lineHeight,
                        fontWeight = MaterialTheme.typography.bodySmall.fontWeight,
                        overflow = TextOverflow.Ellipsis,
                        maxLines = 3,
                        textDecoration = textDecoration
                    )

                    Spacer(modifier = Modifier.height(2.dp))

                    if (expanded) {
                        Spacer(modifier = Modifier.height(10.dp))
                    }

                    AnimatedVisibility(
                        visible = expanded,
                        enter = fadeIn() + expandVertically(),
                        exit = shrinkVertically() + fadeOut()
                    ) {
                        Row(
                            horizontalArrangement = Arrangement.SpaceBetween,
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            if (module.hasActionScript) {
                                FilledTonalButton(
                                    modifier = Modifier.defaultMinSize(52.dp, 32.dp),
                                    enabled = !module.remove && module.enabled && filterZygiskModules,
                                    onClick = {
                                        navigator.navigate(ExecuteModuleActionScreenDestination(module.id))
                                        viewModel.markNeedRefresh()
                                    },
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.Outlined.Terminal,
                                        contentDescription = null
                                    )
                                    if (!module.hasWebUi && updateUrl.isEmpty()) {
                                        Text(
                                            modifier = Modifier.padding(start = 7.dp),
                                            text = stringResource(R.string.action),
                                            fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                            fontSize = MaterialTheme.typography.labelMedium.fontSize
                                        )
                                    }
                                }

                                Spacer(modifier = Modifier.weight(0.1f, true))
                            }

                            if (module.hasWebUi) {
                                FilledTonalButton(
                                    modifier = Modifier.defaultMinSize(52.dp, 32.dp),
                                    enabled = !module.remove && module.enabled && filterZygiskModules,
                                    onClick = { onClick(module) },
                                    interactionSource = interactionSource,
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.AutoMirrored.Outlined.Wysiwyg,
                                        contentDescription = null
                                    )
                                    if (!module.hasActionScript && updateUrl.isEmpty()) {
                                        Text(
                                            modifier = Modifier.padding(start = 7.dp),
                                            fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                            fontSize = MaterialTheme.typography.labelMedium.fontSize,
                                            text = stringResource(R.string.open)
                                        )
                                    }
                                }

                                Spacer(modifier = Modifier.weight(0.1f, true))
                            }

                            if (module.donate.isNotEmpty()) {
                                val donateUrl = module.donate
                                val ctx = LocalContext.current
                                FilledTonalButton(
                                    modifier = Modifier.defaultMinSize(52.dp, 32.dp),
                                    enabled = !module.remove && module.enabled && filterZygiskModules,
                                    onClick = {
                                        try {
                                            val intent = Intent(Intent.ACTION_VIEW, donateUrl.toUri())
                                            ctx.startActivity(intent)
                                        } catch (e: Exception) {
                                            Toast.makeText(ctx, "Invalid donate url", Toast.LENGTH_SHORT).show()
                                        }
                                    },
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.Outlined.AttachMoney,
                                        contentDescription = null
                                    )
                                    if (!module.hasActionScript && !module.hasWebUi && updateUrl.isEmpty()) {
                                        Text(
                                            modifier = Modifier.padding(start = 7.dp),
                                            fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                            fontSize = MaterialTheme.typography.labelMedium.fontSize,
                                            text = stringResource(R.string.donate)
                                        )
                                    }
                                }
                            }

                            Spacer(modifier = Modifier.weight(1f, true))

                            if (updateUrl.isNotEmpty() && !module.remove && !module.update) {
                                Button(
                                    modifier = Modifier.defaultMinSize(52.dp, 32.dp),
                                    enabled = !module.remove,
                                    onClick = { onUpdate(module) },
                                    shape = ButtonDefaults.textShape,
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.Outlined.Download,
                                        contentDescription = null
                                    )
                                    if (!module.hasActionScript || !module.hasWebUi || module.donate.isNotEmpty()) {
                                        Text(
                                            modifier = Modifier.padding(start = 7.dp),
                                            fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                            fontSize = MaterialTheme.typography.labelMedium.fontSize,
                                            text = stringResource(R.string.module_update)
                                        )
                                    }
                                }

                                Spacer(modifier = Modifier.weight(0.1f, true))
                            }

                            if (module.remove) {
                                FilledTonalButton(
                                    modifier = Modifier.defaultMinSize(52.dp, 32.dp),
                                    onClick = { onRestore(module) },
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.Outlined.Restore,
                                        contentDescription = null
                                    )
                                    if (!module.hasActionScript && !module.hasWebUi && updateUrl.isEmpty() && module.donate.isEmpty()) {
                                        Text(
                                            modifier = Modifier.padding(start = 7.dp),
                                            fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                            fontSize = MaterialTheme.typography.labelMedium.fontSize,
                                            text = stringResource(R.string.restore)
                                        )
                                    }
                                }
                            } else {
                                FilledTonalButton(
                                    modifier = Modifier.defaultMinSize(52.dp, 32.dp),
                                    enabled = true,
                                    onClick = { onUninstall(module) },
                                    contentPadding = ButtonDefaults.TextButtonContentPadding
                                ) {
                                    Icon(
                                        modifier = Modifier.size(20.dp),
                                        imageVector = Icons.Outlined.Delete,
                                        contentDescription = null
                                    )
                                    if (!module.hasActionScript && !module.hasWebUi && updateUrl.isEmpty()) {
                                        Text(
                                            modifier = Modifier.padding(start = 7.dp),
                                            fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                                            fontSize = MaterialTheme.typography.labelMedium.fontSize,
                                            text = stringResource(R.string.uninstall)
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

fun formatSize(size: Long): String {
    if (size == 0L) return "null"
    val kb = 1024
    val mb = kb * 1024
    val gb = mb * 1024
    return when {
        size >= gb -> "%.2f GB".format(size.toDouble() / gb)
        size >= mb -> "%.2f MB".format(size.toDouble() / mb)
        size >= kb -> "%.2f KB".format(size.toDouble() / kb)
        else -> "$size B"
    }
}

@Preview
@Composable
fun ModuleItemPreview() {
    val module = ModuleViewModel.ModuleInfo(
        id = "id",
        name = "name",
        version = "version",
        versionCode = 1,
        author = "author",
        description = "I am a test module and i do nothing but show a very long description",
        enabled = true,
        update = true,
        remove = false,
        updateJson = "",
        hasWebUi = false,
        hasActionScript = false,
        size = 12345678L,
        banner = "",
        zygiskRequired = false,
        isMetaModule = false,
        actionIconPath = null,
        webUiIconPath = null,
        donate = ""
    )
    ModuleItem(
        EmptyDestinationsNavigator,
        module,
        "",
        {},
        {},
        {},
        {},
        {},
        false,
        {}
    )
}
