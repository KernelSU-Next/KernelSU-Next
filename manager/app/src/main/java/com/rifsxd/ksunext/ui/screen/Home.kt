package com.rifsxd.ksunext.ui.screen

import android.content.Context
import android.os.Build
import android.os.PowerManager
import android.os.Handler
import android.os.Looper
import android.system.Os
import android.widget.Toast
import androidx.annotation.StringRes
import androidx.compose.animation.*
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.*
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.intl.Locale
import androidx.compose.ui.text.toUpperCase
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.core.content.pm.PackageInfoCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.dergoogler.mmrl.ui.component.LabelItem
import com.dergoogler.mmrl.ui.component.LabelItemDefaults
import com.dergoogler.mmrl.ui.component.text.TextRow
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.InstallScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.rifsxd.ksunext.*
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.component.rememberConfirmDialog
import com.rifsxd.ksunext.ui.util.*
import com.rifsxd.ksunext.ui.util.module.LatestVersionInfo
import com.rifsxd.ksunext.ui.viewmodel.ModuleViewModel
import java.util.*

@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>(start = true)
@Composable
fun HomeScreen(navigator: DestinationsNavigator) {
    val kernelVersion = getKernelVersion()
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())

    val isManager = Natives.becomeManager(ksuApp.packageName)
    val ksuVersion = if (isManager) Natives.version else null

    Scaffold(
        topBar = {
            TopBar(
                kernelVersion,
                ksuVersion,
                onInstallClick = {
                    navigator.navigate(InstallScreenDestination)
                },
                scrollBehavior = scrollBehavior
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            val lkmMode = ksuVersion?.let {
                if (it >= Natives.MINIMAL_SUPPORTED_KERNEL_LKM && kernelVersion.isGKI()) Natives.isLkmMode else null
            }

            val moduleViewModel: ModuleViewModel = viewModel()
            val moduleUpdateCount = moduleViewModel.moduleList.count { 
                // Only count modules when update available (updateUrl is not empty)
                moduleViewModel.checkUpdate(it).first.isNotEmpty()
            }

            StatusCard(kernelVersion, ksuVersion, lkmMode, moduleUpdateCount) {
                navigator.navigate(InstallScreenDestination)
            }
            if (isManager && Natives.requireNewKernel()) {
                WarningCard(
                    stringResource(id = R.string.require_kernel_version).format(
                        ksuVersion, Natives.MINIMAL_SUPPORTED_KERNEL
                    )
                )
            }
            if (ksuVersion != null && !rootAvailable()) {
                WarningCard(
                    stringResource(id = R.string.grant_root_failed)
                )
            }
            val checkUpdate =
                LocalContext.current.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    .getBoolean("check_update", false)
            if (checkUpdate) {
                UpdateCard()
            }
            //NextCard()
            InfoCard()
            IssueReportCard()
            //EXperimentalCard()
            Spacer(Modifier)
        }
    }
}

@Composable
fun UpdateCard() {
    val context = LocalContext.current
    val latestVersionInfo = LatestVersionInfo()
    val newVersion by produceState(initialValue = latestVersionInfo) {
        value = withContext(Dispatchers.IO) {
            checkNewVersion()
        }
    }

    val currentVersionCode = getManagerVersion(context).second
    val newVersionCode = newVersion.versionCode
    val newVersionUrl = newVersion.downloadUrl
    val changelog = newVersion.changelog

    val uriHandler = LocalUriHandler.current
    val title = stringResource(id = R.string.module_changelog)
    val updateText = stringResource(id = R.string.module_update)

    AnimatedVisibility(
        visible = newVersionCode > currentVersionCode,
        enter = fadeIn() + expandVertically(),
        exit = shrinkVertically() + fadeOut()
    ) {
        val updateDialog = rememberConfirmDialog(onConfirm = { uriHandler.openUri(newVersionUrl) })
        WarningCard(
            message = stringResource(id = R.string.new_version_available).format(newVersionCode),
            MaterialTheme.colorScheme.outlineVariant
        ) {
            if (changelog.isEmpty()) {
                uriHandler.openUri(newVersionUrl)
            } else {
                updateDialog.showConfirm(
                    title = title,
                    content = changelog,
                    markdown = true,
                    confirm = updateText
                )
            }
        }
    }
}

@Composable
fun RebootDropdownItem(@StringRes id: Int, reason: String = "") {
    DropdownMenuItem(text = {
        Text(stringResource(id))
    }, onClick = {
        reboot(reason)
    })
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    kernelVersion: KernelVersion,
    ksuVersion: Int?,
    onInstallClick: () -> Unit,
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    TopAppBar(
        title = { Text(stringResource(R.string.app_name)) },
        actions = {
            if (ksuVersion != null) {
                if (kernelVersion.isGKI()) {
                    IconButton(onClick = onInstallClick) {
                        Icon(
                            imageVector = Icons.Filled.Archive,
                            contentDescription = stringResource(id = R.string.install)
                        )
                    }
                }
            }

            if (ksuVersion != null) {
                var showDropdown by remember { mutableStateOf(false) }
                IconButton(onClick = {
                    showDropdown = true
                }) {
                    Icon(
                        imageVector = Icons.Filled.Refresh,
                        contentDescription = stringResource(id = R.string.reboot)
                    )

                    DropdownMenu(expanded = showDropdown, onDismissRequest = {
                        showDropdown = false
                    }) {
                        RebootDropdownItem(id = R.string.reboot)

                        val pm =
                            LocalContext.current.getSystemService(Context.POWER_SERVICE) as PowerManager?
                        @Suppress("DEPRECATION")
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && pm?.isRebootingUserspaceSupported == true) {
                            RebootDropdownItem(id = R.string.reboot_userspace, reason = "userspace")
                        }
                        RebootDropdownItem(id = R.string.reboot_recovery, reason = "recovery")
                        RebootDropdownItem(id = R.string.reboot_bootloader, reason = "bootloader")
                        RebootDropdownItem(id = R.string.reboot_download, reason = "download")
                        RebootDropdownItem(id = R.string.reboot_edl, reason = "edl")
                    }
                }
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}

@Composable
fun getSeasonalIcon(): ImageVector {
    val month = Calendar.getInstance().get(Calendar.MONTH) // 0-11 for January-December
    return when (month) {
        Calendar.DECEMBER, Calendar.JANUARY, Calendar.FEBRUARY -> Icons.Filled.AcUnit // Winter
        Calendar.MARCH, Calendar.APRIL, Calendar.MAY -> Icons.Filled.Spa // Spring
        Calendar.JUNE, Calendar.JULY, Calendar.AUGUST -> Icons.Filled.WbSunny // Summer
        Calendar.SEPTEMBER, Calendar.OCTOBER, Calendar.NOVEMBER -> Icons.Filled.Forest // Fall
        else -> Icons.Filled.Whatshot // Fallback icon
    }
}

@Composable
private fun StatusCard(
    kernelVersion: KernelVersion,
    ksuVersion: Int?,
    lkmMode: Boolean?,
    moduleUpdateCount: Int = 0,
    onClickInstall: () -> Unit = {}
) {
    val context = LocalContext.current
    var tapCount by remember { mutableStateOf(0) }

    ElevatedCard(
        colors = CardDefaults.elevatedCardColors(containerColor = run {
            if (ksuVersion != null) MaterialTheme.colorScheme.secondaryContainer
            else MaterialTheme.colorScheme.errorContainer
        })
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable {
                    tapCount++
                    if (tapCount == 10) {
                        Toast.makeText(context, "Never gonna give you up! 💜", Toast.LENGTH_SHORT).show()
                        val url = "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
                        val intent = android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(url))
                        intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK)
                        if (ksuVersion != null) {
                            context.startActivity(intent)
                        } else {
                            onClickInstall()
                        }
                    } else if (ksuVersion == null && kernelVersion.isGKI()) {
                        onClickInstall()
                    }
                }
                .padding(24.dp), verticalAlignment = Alignment.CenterVertically) {
            when {
                ksuVersion != null -> {
                    val workingMode = when {
                        lkmMode == true -> "LKM"
                        lkmMode == false || kernelVersion.isGKI() -> "GKI2"
                        lkmMode == null && kernelVersion.isULegacy() -> "U-LEGACY"
                        lkmMode == null && kernelVersion.isLegacy() -> "LEGACY"
                        lkmMode == null && kernelVersion.isGKI1() -> "GKI1"
                        else -> "NON-STANDARD"
                    }

                    Icon(
                        getSeasonalIcon(), // Use dynamic seasonal icon
                        contentDescription = stringResource(R.string.home_working)
                    )
                    Column(
                        modifier = Modifier.padding(start = 20.dp),
                        verticalArrangement = Arrangement.spacedBy(4.dp)
                    ) {
                        val labelStyle = LabelItemDefaults.style
                        TextRow(
                            trailingContent = {
                                LabelItem(
                                    icon = if (Natives.isSafeMode) {
                                        {
                                            Icon(
                                                tint = labelStyle.contentColor,
                                                imageVector = Icons.Filled.Security,
                                                contentDescription = null
                                            )
                                        }
                                    } else {
                                        null
                                    },
                                    text = {
                                        Text(
                                            text = workingMode,
                                            style = labelStyle.textStyle.copy(color = labelStyle.contentColor),
                                        )
                                    }
                                )
                            }
                        ) {
                            Text(
                                text = stringResource(id = R.string.home_working),
                                style = MaterialTheme.typography.titleMedium
                            )
                        }

                        Text(
                            text = stringResource(R.string.home_working_version, ksuVersion),
                            style = MaterialTheme.typography.bodyMedium
                        )

                        Text(
                            text = stringResource(
                                R.string.home_superuser_count, getSuperuserCount()
                            ), style = MaterialTheme.typography.bodyMedium
                        )

                        Text(
                            text = stringResource(R.string.home_module_count, getModuleCount()),
                            style = MaterialTheme.typography.bodyMedium
                        )

                        if (moduleUpdateCount > 0) {
                            Text(
                                text = stringResource(R.string.home_module_update_count, moduleUpdateCount),
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.primary
                            )
                        }

                        val suSFS = getSuSFS()
                        if (suSFS == "Supported") {
                            Text(
                                text = "SuSFS: " + stringResource(R.string.susfs_supported),
                                style = MaterialTheme.typography.bodyMedium
                            )
                        }
                    }
                }

                kernelVersion.isGKI() -> {
                    Icon(Icons.Filled.Report, stringResource(R.string.home_not_installed))
                    Column(Modifier.padding(start = 20.dp)) {
                        Text(
                            text = stringResource(R.string.home_not_installed),
                            style = MaterialTheme.typography.titleMedium
                        )
                        Spacer(Modifier.height(4.dp))
                        Text(
                            text = stringResource(R.string.home_click_to_install),
                            style = MaterialTheme.typography.bodyMedium
                        )
                    }
                }

                else -> {
                    Icon(Icons.Filled.Dangerous, stringResource(R.string.home_failure))
                    Column(Modifier.padding(start = 20.dp)) {
                        Text(
                            text = stringResource(R.string.home_failure),
                            style = MaterialTheme.typography.titleMedium
                        )
                        Spacer(Modifier.height(4.dp))
                        Text(
                            text = stringResource(R.string.home_failure_tip),
                            style = MaterialTheme.typography.bodyMedium
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun WarningCard(
    message: String, color: Color = MaterialTheme.colorScheme.error, onClick: (() -> Unit)? = null
) {
    ElevatedCard(
        colors = CardDefaults.elevatedCardColors(
            containerColor = color
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .then(onClick?.let { Modifier.clickable { it() } } ?: Modifier)
                .padding(24.dp)
        ) {
            Text(
                text = message, style = MaterialTheme.typography.bodyMedium
            )
        }
    }
}

@Composable
private fun InfoCard() {
    val context = LocalContext.current

    val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    var useOverlayFs by rememberSaveable {
        mutableStateOf(prefs.getBoolean("use_overlay_fs", false))
    }

    val isManager = Natives.becomeManager(ksuApp.packageName)
    val ksuVersion = if (isManager) Natives.version else null

    LaunchedEffect(Unit) {
        useOverlayFs = prefs.getBoolean("use_overlay_fs", false)
    }

    ElevatedCard {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(start = 24.dp, top = 24.dp, end = 24.dp, bottom = 16.dp)
        ) {
            var expanded by rememberSaveable { mutableStateOf(false) }

            @Composable
            fun InfoCardItem(label: String, content: String, icon: Any? = null) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    if (icon != null) {
                        when (icon) {
                            is ImageVector -> Icon(
                                imageVector = icon,
                                contentDescription = null,
                                modifier = Modifier.padding(end = 20.dp)
                            )
                            is Painter -> Icon(
                                painter = icon,
                                contentDescription = null,
                                modifier = Modifier.padding(end = 20.dp)
                            )
                        }
                    }
                    Column {
                        Text(
                            text = label,
                            style = MaterialTheme.typography.bodyLarge
                        )
                        Text(
                            text = content,
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(top = 4.dp)
                        )
                    }
                }
            }

            Column {
                val managerVersion = getManagerVersion(context)
                InfoCardItem(
                    label = stringResource(R.string.home_manager_version),
                    content = "${managerVersion.first} (${managerVersion.second})",
                    icon = painterResource(R.drawable.ic_ksu_next),
                )

                if (Natives.version >= Natives.MINIMAL_SUPPORTED_HOOK_MODE) {
                    Spacer(Modifier.height(16.dp))
                    InfoCardItem(
                        label = stringResource(R.string.hook_mode),
                        content = Natives.getHookMode() ?: stringResource(R.string.unavailable),
                        icon = Icons.Filled.Phishing,
                    )
                }

                if (ksuVersion != null) {
                    Spacer(Modifier.height(16.dp))
                    InfoCardItem(
                        label = stringResource(R.string.home_mount_system),
                        content = currentMountSystem().ifEmpty { stringResource(R.string.unavailable) },
                        icon = Icons.Filled.SettingsSuggest,
                    )
                    

                    val suSFS = getSuSFS()
                    if (suSFS == "Supported") {
                        val isSUS_SU = getSuSFSFeatures() == "CONFIG_KSU_SUSFS_SUS_SU"
                        val susSUMode = if (isSUS_SU) {
                            val mode = susfsSUS_SU_Mode()
                            val modeString =
                                if (mode == "2") stringResource(R.string.enabled) else stringResource(R.string.disabled)
                            "| SuS SU: $modeString"
                        } else ""
                        Spacer(Modifier.height(16.dp))
                        InfoCardItem(
                            label = stringResource(R.string.home_susfs_version),
                            content = "${getSuSFSVersion()} (${getSuSFSVariant()}) $susSUMode",
                            icon = painterResource(R.drawable.ic_sus),
                        )
                    }
                }

                if (!expanded) {
                    Spacer(Modifier.height(12.dp))
                    Row(
                        modifier = Modifier
                            .fillMaxWidth(),
                        horizontalArrangement = Arrangement.Center
                    ) {
                        IconButton(
                            onClick = { expanded = true },
                            modifier = Modifier.size(36.dp)
                        ) {
                            Icon(
                                imageVector = Icons.Filled.KeyboardArrowDown,
                                contentDescription = "Show more"
                            )
                        }
                    }
                }

                AnimatedVisibility(visible = expanded) {
                    val uname = Os.uname()
                    Column {
                        Spacer(Modifier.height(16.dp))
                        InfoCardItem(
                            label = stringResource(R.string.home_kernel),
                            content = "${uname.release} (${uname.machine})",
                            icon = painterResource(R.drawable.ic_linux),
                        )

                        Spacer(Modifier.height(16.dp))
                        InfoCardItem(
                            label = stringResource(R.string.home_android),
                            content = "${Build.VERSION.RELEASE} (${Build.VERSION.SDK_INT})",
                            icon = Icons.Filled.Android,
                        )

                        Spacer(Modifier.height(16.dp))
                        InfoCardItem(
                            label = stringResource(R.string.home_abi),
                            content = Build.SUPPORTED_ABIS.joinToString(", "),
                            icon = Icons.Filled.Memory,
                        )

                        Spacer(Modifier.height(16.dp))
                        InfoCardItem(
                            label = stringResource(R.string.home_selinux_status),
                            content = getSELinuxStatus(),
                            icon = Icons.Filled.Security,
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun NextCard() {
    val uriHandler = LocalUriHandler.current
    val url = stringResource(R.string.home_next_kernelsu_repo)

    ElevatedCard {

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable {
                    uriHandler.openUri(url)
                }
                .padding(24.dp), verticalAlignment = Alignment.CenterVertically) {
            Column {
                Text(
                    text = stringResource(R.string.home_next_kernelsu),
                    style = MaterialTheme.typography.titleSmall
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    text = stringResource(R.string.home_next_kernelsu_body),
                    style = MaterialTheme.typography.bodyMedium
                )
            }
        }
    }
}

@Composable
fun EXperimentalCard() {
    /*val uriHandler = LocalUriHandler.current
    val url = stringResource(R.string.home_experimental_kernelsu_repo)
    */

    ElevatedCard {

        Row(
            modifier = Modifier
                .fillMaxWidth()
                /*.clickable {
                    uriHandler.openUri(url)
                }
                */
                .padding(24.dp), verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = stringResource(R.string.home_experimental_kernelsu),
                    style = MaterialTheme.typography.titleSmall
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    text = stringResource(R.string.home_experimental_kernelsu_body),
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    text = stringResource(R.string.home_experimental_kernelsu_body_point_1),
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(Modifier.height(2.dp))
                Text(
                    text = stringResource(R.string.home_experimental_kernelsu_body_point_2),
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(Modifier.height(2.dp))
                Text(
                    text = stringResource(R.string.home_experimental_kernelsu_body_point_3),
                    style = MaterialTheme.typography.bodyMedium
                )
            }
        }
    }
}

@Composable
fun IssueReportCard() {
    val uriHandler = LocalUriHandler.current
    val githubIssueUrl = stringResource(R.string.issue_report_github_link)
    val telegramUrl = stringResource(R.string.issue_report_telegram_link)

    ElevatedCard {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.issue_report_title),
                    style = MaterialTheme.typography.titleSmall
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    text = stringResource(R.string.issue_report_body),
                    style = MaterialTheme.typography.bodyMedium
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    text = stringResource(R.string.issue_report_body_2),
                    style = MaterialTheme.typography.bodyMedium
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                IconButton(onClick = { uriHandler.openUri(githubIssueUrl) }) {
                    Icon(
                        painter = painterResource(R.drawable.ic_github),
                        contentDescription = stringResource(R.string.issue_report_github),
                    )
                }
                IconButton(onClick = { uriHandler.openUri(telegramUrl) }) {
                    Icon(
                        painter = painterResource(R.drawable.ic_telegram),
                        contentDescription = stringResource(R.string.issue_report_telegram),
                    )
                }
            }
        }
    }
}

fun getManagerVersion(context: Context): Pair<String, Long> {
    val packageInfo = context.packageManager.getPackageInfo(context.packageName, 0)!!
    val versionCode = PackageInfoCompat.getLongVersionCode(packageInfo)
    return Pair(packageInfo.versionName!!, versionCode)
}

@Preview
@Composable
private fun StatusCardPreview() {
    Column {
        StatusCard(KernelVersion(5, 10, 101), 1, null)
        StatusCard(KernelVersion(5, 10, 101), 20000, true)
        StatusCard(KernelVersion(5, 10, 101), null, true)
        StatusCard(KernelVersion(4, 10, 101), null, false)
    }
}

@Preview
@Composable
private fun WarningCardPreview() {
    Column {
        WarningCard(message = "Warning message")
        WarningCard(
            message = "Warning message ",
            MaterialTheme.colorScheme.outlineVariant,
            onClick = {})
    }
}
