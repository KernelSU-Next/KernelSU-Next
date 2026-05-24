package com.rifsxd.ksunext.ui.screen

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.system.OsConstants
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.background
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import com.rifsxd.ksunext.ui.LocalScrollState
import com.rifsxd.ksunext.ui.rememberScrollConnection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.core.content.edit
import androidx.compose.ui.graphics.Color
import androidx.compose.material3.ListItemDefaults
import com.maxkeppeker.sheets.core.models.base.Header
import com.maxkeppeker.sheets.core.models.base.IconSource
import com.maxkeppeker.sheets.core.models.base.rememberUseCaseState
import com.maxkeppeler.sheets.list.ListDialog
import com.maxkeppeler.sheets.list.models.ListOption
import com.maxkeppeler.sheets.list.models.ListSelection
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.*
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.navigation.EmptyDestinationsNavigator
import com.rifsxd.ksunext.BuildConfig
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.component.*
import com.rifsxd.ksunext.ui.util.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

/**
 * @author weishu
 * @date 2023/1/1.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun SettingScreen(navigator: DestinationsNavigator) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    val snackBarHost = LocalSnackbarHost.current

    val isManager = Natives.isManager
    val ksuVersion = if (isManager) Natives.version else null

    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    val scrollState = LocalScrollState.current
    val isNavBarHidden = scrollState?.isScrollingDown?.value ?: false
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + if (isNavBarHidden) 0.dp else 112.dp

    val bottomBarScrollState = LocalScrollState.current
    val bottomBarScrollConnection = if (bottomBarScrollState != null) {
        rememberScrollConnection(
            isScrollingDown = bottomBarScrollState.isScrollingDown,
            scrollOffset = bottomBarScrollState.scrollOffset,
            previousScrollOffset = bottomBarScrollState.previousScrollOffset,
            threshold = 30f
        )
    } else null

    val aboutDialog = rememberCustomDialog {
        AboutDialog(it)
    }
    val loadingDialog = rememberLoadingDialog()

    val exportBugreportLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.CreateDocument("application/gzip")
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch(Dispatchers.IO) {
            loadingDialog.show()
            context.contentResolver.openOutputStream(uri)?.use { output ->
                getBugreportFile(context).inputStream().use {
                    it.copyTo(output)
                }
            }
            loadingDialog.hide()
            snackBarHost.showSnackbar(context.getString(R.string.log_saved))
        }
    }

    var suCompatStatus by rememberSaveable { mutableStateOf("") }
    var kernelUmountStatus by rememberSaveable { mutableStateOf("") }
    var adbRootStatus by rememberSaveable { mutableStateOf("") }
    var selinuxHideStatus by rememberSaveable { mutableStateOf("") }
    var avcSpoofStatus by rememberSaveable { mutableStateOf("") }

    LaunchedEffect(Unit) {
        suCompatStatus = getFeatureStatus("su_compat")
        kernelUmountStatus = getFeatureStatus("kernel_umount")
        adbRootStatus = getFeatureStatus("adb_root")
        selinuxHideStatus = getFeatureStatus("selinux_hide")
        avcSpoofStatus = getFeatureStatus("avc_spoof")
    }

    Scaffold(
        topBar = {
            TopBar(scrollBehavior = scrollBehavior)
        },
        snackbarHost = { SnackbarHost(snackBarHost, modifier = Modifier.padding(bottom = navBarPadding)) },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .let { modifier ->
                    if (bottomBarScrollConnection != null) {
                        modifier
                            .nestedScroll(bottomBarScrollConnection)
                            .nestedScroll(scrollBehavior.nestedScrollConnection)
                    } else {
                        modifier.nestedScroll(scrollBehavior.nestedScrollConnection)
                    }
                }
                .verticalScroll(rememberScrollState())
                .padding(top = 16.dp)
                .padding(bottom = navBarPadding)
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            if (ksuVersion != null) {
                KernelFeaturesCard(
                    suCompatStatus = suCompatStatus,
                    kernelUmountStatus = kernelUmountStatus,
                    adbRootStatus = adbRootStatus,
                    selinuxHideStatus = selinuxHideStatus,
                    avcSpoofStatus = avcSpoofStatus,
                    scope = scope
                )
                SecurityCard(
                    navigator = navigator,
                    loadingDialog = loadingDialog
                )
            }

            AppSettingsCard(
                navigator = navigator,
                prefs = prefs,
                aboutDialog = aboutDialog,
                exportBugreportLauncher = exportBugreportLauncher,
                loadingDialog = loadingDialog,
                scope = scope,
                context = context
            )

            Spacer(Modifier)
        }
    }
}

@Composable
private fun KernelFeaturesCard(
    suCompatStatus: String,
    kernelUmountStatus: String,
    adbRootStatus: String,
    selinuxHideStatus: String,
    avcSpoofStatus: String,
    scope: kotlinx.coroutines.CoroutineScope
) {
    val context = LocalContext.current

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            var umountChecked by rememberSaveable {
                mutableStateOf(Natives.isDefaultUmountModules())
            }

            SwitchItem(
                icon = Icons.Filled.FolderDelete,
                title = stringResource(R.string.settings_umount_modules_default),
                summary = stringResource(R.string.settings_umount_modules_default_summary),
                checked = umountChecked,
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                colors = ListItemDefaults.colors(containerColor = Color.Transparent)
            ) {
                if (Natives.setDefaultUmountModules(it)) {
                    umountChecked = it
                }
            }

            if (suCompatStatus == "supported") {
                var isSuEnabled by rememberSaveable {
                    mutableStateOf(Natives.isSuEnabled())
                }
                SwitchItem(
                    icon = Icons.Filled.RemoveModerator,
                    title = stringResource(R.string.settings_enable_su),
                    summary = stringResource(R.string.settings_enable_su_summary),
                    checked = isSuEnabled,
                    modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent)
                ) { checked ->
                    val prefsLocal = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    if (Natives.setSuEnabled(checked)) {
                        execKsud("feature save", true)
                        prefsLocal.edit { putInt("su_compat_mode", if (checked) 0 else 2) }
                        isSuEnabled = checked
                    }
                }
            }

            if (kernelUmountStatus == "supported") {
                var isKernelUmountEnabled by rememberSaveable {
                    mutableStateOf(Natives.isKernelUmountEnabled())
                }
                SwitchItem(
                    icon = Icons.Filled.RemoveCircle,
                    title = stringResource(id = R.string.settings_enable_kernel_umount),
                    summary = stringResource(id = R.string.settings_enable_kernel_umount_summary),
                    checked = isKernelUmountEnabled,
                    modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent)
                ) { checked ->
                    val prefsLocal = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    if (Natives.setKernelUmountEnabled(checked)) {
                        execKsud("feature save", true)
                        prefsLocal.edit { putInt("kernel_umount_mode", if (checked) 0 else 2) }
                        isKernelUmountEnabled = checked
                    }
                }
            }

            if (adbRootStatus == "supported") {
                var isAdbRootEnabled by rememberSaveable {
                    mutableStateOf(Natives.isAdbRootEnabled())
                }
                SwitchItem(
                    icon = Icons.Filled.Usb,
                    title = stringResource(id = R.string.settings_adb_root),
                    summary = stringResource(id = R.string.settings_adb_root_summary),
                    checked = isAdbRootEnabled,
                    modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent)
                ) { checked ->
                    val prefsLocal = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    if (Natives.setAdbRootEnabled(checked)) {
                        execKsud("feature save", true)
                        prefsLocal.edit { putInt("adb_root_mode", if (checked) 0 else 2) }
                        com.topjohnwu.superuser.ShellUtils.fastCmd("setprop ctl.restart adbd")
                        isAdbRootEnabled = checked
                    }
                }
            }

            if (selinuxHideStatus == "supported") {
                var isSelinuxHideEnabled by rememberSaveable {
                    mutableStateOf(Natives.isSelinuxHideEnabled())
                }
                SwitchItem(
                    icon = Icons.Filled.Policy,
                    title = stringResource(id = R.string.settings_selinux_hide),
                    summary = stringResource(id = R.string.settings_selinux_hide_summary),
                    checked = isSelinuxHideEnabled,
                    modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent)
                ) { checked ->
                    scope.launch(Dispatchers.IO) {
                        val prefsLocal = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                        val status = Natives.setSelinuxHideEnabled(checked)
                        execKsud("feature save", true)
                        prefsLocal.edit { putInt("selinux_hide_mode", if (checked) 0 else 2) }
                        isSelinuxHideEnabled = checked
                        when (status) {
                            0 -> {}
                            -OsConstants.EAGAIN -> {
                                withContext(Dispatchers.Main) {
                                    Toast.makeText(context, R.string.settings_selinux_hide_reboot_required,
                                        Toast.LENGTH_LONG).show()
                                }
                            }
                            else -> {
                                withContext(Dispatchers.Main) {
                                    Toast.makeText(context, context.getString(R.string.settings_selinux_hide_failed, status),
                                        Toast.LENGTH_LONG).show()
                                }
                            }
                        }
                    }
                }
            }

            if (avcSpoofStatus == "supported") {
                var isAvcSpoofEnabled by rememberSaveable {
                    mutableStateOf(Natives.isAvcSpoofEnabled())
                }
                SwitchItem(
                    icon = Icons.Filled.Shield,
                    title = stringResource(id = R.string.settings_enable_avc_spoof),
                    summary = stringResource(id = R.string.settings_enable_avc_spoof_summary),
                    checked = isAvcSpoofEnabled,
                    modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                    colors = ListItemDefaults.colors(containerColor = Color.Transparent)
                ) { checked ->
                    val prefsLocal = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    if (Natives.setAvcSpoofEnabled(checked)) {
                        execKsud("feature save", true)
                        prefsLocal.edit { putInt("avc_spoof_mode", if (checked) 0 else 2) }
                        isAvcSpoofEnabled = checked
                    }
                }
            }
        }
    }
}

@Composable
private fun SecurityCard(
    navigator: DestinationsNavigator,
    loadingDialog: LoadingDialogHandle
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            var isSelinuxPermissive by rememberSaveable {
                mutableStateOf(getSelinuxEnforce() == false)
            }

            SwitchItem(
                icon = Icons.Filled.Security,
                title = stringResource(R.string.set_selinux),
                summary = stringResource(R.string.set_selinux_summary),
                checked = isSelinuxPermissive,
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                colors = ListItemDefaults.colors(containerColor = Color.Transparent)
            ) { checked ->
                val shouldEnforce = !checked
                if (setSelinuxEnforce(shouldEnforce)) {
                    isSelinuxPermissive = !shouldEnforce
                }
            }

            ListItem(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { navigator.navigate(AppProfileTemplateScreenDestination) },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Filled.Fence, null) },
                headlineContent = {
                    Text(
                        text = stringResource(R.string.settings_profile_template),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                supportingContent = {
                    Text(stringResource(R.string.settings_profile_template_summary))
                }
            )

            ListItem(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { navigator.navigate(BackupRestoreScreenDestination) },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Filled.Backup, null) },
                headlineContent = {
                    Text(
                        text = stringResource(R.string.backup_restore),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            )

            ListItem(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { navigator.navigate(DeveloperScreenDestination) },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Filled.DeveloperBoard, null) },
                headlineContent = {
                    Text(
                        text = stringResource(R.string.developer),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            )

            if (Natives.isLkmMode) {
                UninstallItem(
                    navigator = navigator,
                    withLoading = { loadingDialog.withLoading(it) },
                    modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp))
                )
            }
        }
    }
}

@Composable
private fun AppSettingsCard(
    navigator: DestinationsNavigator,
    prefs: android.content.SharedPreferences,
    aboutDialog: DialogHandle,
    exportBugreportLauncher: androidx.activity.result.ActivityResultLauncher<String>,
    loadingDialog: LoadingDialogHandle,
    scope: kotlinx.coroutines.CoroutineScope,
    context: android.content.Context
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            var checkUpdate by rememberSaveable {
                mutableStateOf(prefs.getBoolean("check_update", true))
            }

            SwitchItem(
                icon = Icons.Filled.Update,
                title = stringResource(R.string.settings_check_update),
                summary = stringResource(R.string.settings_check_update_summary),
                checked = checkUpdate,
                modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)),
                colors = ListItemDefaults.colors(containerColor = Color.Transparent)
            ) {
                prefs.edit { putBoolean("check_update", it) }
                checkUpdate = it
            }

            ListItem(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { navigator.navigate(CustomizationScreenDestination) },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Filled.Palette, null) },
                headlineContent = {
                    Text(
                        text = stringResource(R.string.customization),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            )

            var showBottomsheet by remember { mutableStateOf(false) }

            ListItem(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { showBottomsheet = true },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Filled.BugReport, null) },
                headlineContent = {
                    Text(
                        text = stringResource(R.string.export_log),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            )

            if (showBottomsheet) {
                ExportLogBottomSheet(
                    onDismiss = { showBottomsheet = false },
                    onSave = {
                        val formatter = DateTimeFormatter.ofPattern("yyyy-MM-dd_HH_mm")
                        val current = LocalDateTime.now().format(formatter)
                        exportBugreportLauncher.launch("KernelSU_Next_bugreport_${current}.tar.gz")
                        showBottomsheet = false
                    },
                    onShare = {
                        scope.launch {
                            val bugreport = loadingDialog.withLoading {
                                withContext(Dispatchers.IO) {
                                    getBugreportFile(context)
                                }
                            }
                            val uri: Uri = FileProvider.getUriForFile(
                                context,
                                "${BuildConfig.APPLICATION_ID}.fileprovider",
                                bugreport
                            )
                            val shareIntent = Intent(Intent.ACTION_SEND).apply {
                                putExtra(Intent.EXTRA_STREAM, uri)
                                setDataAndType(uri, "application/gzip")
                                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                            }
                            context.startActivity(
                                Intent.createChooser(shareIntent, context.getString(R.string.send_log))
                            )
                        }
                        showBottomsheet = false
                    }
                )
            }

            ListItem(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { aboutDialog.show() },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                leadingContent = { Icon(Icons.Filled.ContactPage, null) },
                headlineContent = {
                    Text(
                        text = stringResource(R.string.about),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                }
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ExportLogBottomSheet(
    onDismiss: () -> Unit,
    onSave: () -> Unit,
    onShare: () -> Unit
) {
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        content = {
            Row(
                modifier = Modifier
                    .padding(10.dp)
                    .align(Alignment.CenterHorizontally)
            ) {
                Box {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        IconButton(
                            onClick = onSave,
                            modifier = Modifier.size(56.dp).clip(CircleShape)
                        ) {
                            Icon(Icons.Filled.Save, contentDescription = null)
                        }
                        Text(
                            text = stringResource(id = R.string.save_log),
                            modifier = Modifier.padding(top = 16.dp),
                            textAlign = TextAlign.Center.also {
                                LineHeightStyle(
                                    alignment = LineHeightStyle.Alignment.Center,
                                    trim = LineHeightStyle.Trim.None
                                )
                            }
                        )
                    }
                }
                Box {
                    Column(
                        modifier = Modifier.padding(16.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        IconButton(
                            onClick = onShare,
                            modifier = Modifier.size(56.dp).clip(CircleShape)
                        ) {
                            Icon(Icons.Filled.Share, contentDescription = null)
                        }
                        Text(
                            text = stringResource(id = R.string.send_log),
                            modifier = Modifier.padding(top = 16.dp),
                            textAlign = TextAlign.Center.also {
                                LineHeightStyle(
                                    alignment = LineHeightStyle.Alignment.Center,
                                    trim = LineHeightStyle.Trim.None
                                )
                            }
                        )
                    }
                }
            }
        }
    )
}

@Composable
fun UninstallItem(
    navigator: DestinationsNavigator,
    withLoading: suspend (suspend () -> Unit) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val uninstallConfirmDialog = rememberConfirmDialog()
    val showTodo = {
        Toast.makeText(context, "TODO", Toast.LENGTH_SHORT).show()
    }
    val uninstallDialog = rememberUninstallDialog { uninstallType ->
        scope.launch {
            val result = uninstallConfirmDialog.awaitConfirm(
                title = context.getString(uninstallType.title),
                content = context.getString(uninstallType.message)
            )
            if (result == ConfirmResult.Confirmed) {
                withLoading {
                    when (uninstallType) {
                        UninstallType.TEMPORARY -> showTodo()
                        UninstallType.PERMANENT -> navigator.navigate(
                            FlashScreenDestination(FlashIt.FlashUninstall)
                        )
                        UninstallType.RESTORE_STOCK_IMAGE -> navigator.navigate(
                            FlashScreenDestination(FlashIt.FlashRestore)
                        )
                        UninstallType.NONE -> Unit
                    }
                }
            }
        }
    }
    val uninstall = stringResource(id = R.string.settings_uninstall)
    ListItem(
        modifier = modifier.clickable { uninstallDialog.show() },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        leadingContent = { Icon(Icons.Filled.Delete, uninstall) },
        headlineContent = {
            Text(
                text = uninstall,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold
            )
        }
    )
}

enum class UninstallType(val title: Int, val message: Int, val icon: ImageVector) {
    TEMPORARY(
        R.string.settings_uninstall_temporary,
        R.string.settings_uninstall_temporary_message,
        Icons.Filled.Delete
    ),
    PERMANENT(
        R.string.settings_uninstall_permanent,
        R.string.settings_uninstall_permanent_message,
        Icons.Filled.DeleteForever
    ),
    RESTORE_STOCK_IMAGE(
        R.string.settings_restore_stock_image,
        R.string.settings_restore_stock_image_message,
        Icons.AutoMirrored.Filled.Undo
    ),
    NONE(0, 0, Icons.Filled.Delete)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun rememberUninstallDialog(onSelected: (UninstallType) -> Unit): DialogHandle {
    return rememberCustomDialog { dismiss ->
        val options = listOf(
            // UninstallType.TEMPORARY,
            UninstallType.PERMANENT,
            UninstallType.RESTORE_STOCK_IMAGE
        )
        val listOptions = options.map {
            ListOption(
                titleText = stringResource(it.title),
                subtitleText = if (it.message != 0) stringResource(it.message) else null,
                icon = IconSource(it.icon)
            )
        }

        var selection = UninstallType.NONE
        ListDialog(
            state = rememberUseCaseState(visible = true, onFinishedRequest = {
                if (selection != UninstallType.NONE) {
                    onSelected(selection)
                }
            }, onCloseRequest = {
                dismiss()
            }),
            header = Header.Default(title = stringResource(R.string.settings_uninstall)),
            selection = ListSelection.Single(
                showRadioButtons = false,
                options = listOptions,
            ) { index, _ ->
                selection = options[index]
            }
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    scrollBehavior: TopAppBarScrollBehavior? = null,
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.settings),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Black,
            )
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}

@Preview
@Composable
private fun SettingsPreview() {
    SettingScreen(EmptyDestinationsNavigator)
}
