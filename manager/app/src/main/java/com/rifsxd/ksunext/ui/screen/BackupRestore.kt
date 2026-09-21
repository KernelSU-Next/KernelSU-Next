package com.rifsxd.ksunext.ui.screen

import android.app.Activity.RESULT_OK
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Backup
import androidx.compose.material.icons.filled.Restore
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.navigation.EmptyDestinationsNavigator
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.ui.LocalScrollState
import com.rifsxd.ksunext.ui.component.rememberLoadingDialog
import com.rifsxd.ksunext.ui.rememberScrollConnection
import com.rifsxd.ksunext.ui.util.LocalSnackbarHost
import com.rifsxd.ksunext.ui.util.reboot
import com.topjohnwu.superuser.ShellUtils
import com.topjohnwu.superuser.io.SuFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.SimpleDateFormat
import java.util.*

private const val BUSYBOX = "/data/adb/ksu/bin/busybox"

/**
 * Backs up /data/adb/modules as a tar into [destUri].
 * Tar is written to cacheDir, streamed to the user-chosen URI, then deleted.
 */
private suspend fun backupModulesToUri(uri: Uri): Boolean = withContext(Dispatchers.IO) {
    val modulesDir = SuFile("/data/adb/modules")
    if (modulesDir.listFiles()?.isEmpty() != false) return@withContext false

    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
    val tmpPath   = "${ksuApp.cacheDir}/modules_backup_$timestamp.tar"

    val tarCmd = "$BUSYBOX tar -cpf '$tmpPath' -C /data/adb/modules \$(ls /data/adb/modules)"
    if (!ShellUtils.fastCmdResult(tarCmd)) return@withContext false

    return@withContext try {
        SuFile(tmpPath).newInputStream().use { input ->
            ksuApp.contentResolver.openOutputStream(uri)?.use { out ->
                input.copyTo(out)
            }
        }
        true
    } catch (e: Exception) {
        e.printStackTrace()
        false
    } finally {
        SuFile(tmpPath).delete()
    }
}

/**
 * Backs up /data/adb/ksu/.allowlist as a tar into [destUri].
 * Tar is written to cacheDir, streamed to the user-chosen URI, then deleted.
 */
private suspend fun backupAllowlistToUri(uri: Uri): Boolean = withContext(Dispatchers.IO) {
    if (!SuFile("/data/adb/ksu/.allowlist").exists()) return@withContext false

    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
    val tmpPath   = "${ksuApp.cacheDir}/allowlist_backup_$timestamp.tar"

    val tarCmd = "$BUSYBOX tar -cpf '$tmpPath' -C /data/adb/ksu .allowlist"
    if (!ShellUtils.fastCmdResult(tarCmd)) return@withContext false

    return@withContext try {
        SuFile(tmpPath).newInputStream().use { input ->
            ksuApp.contentResolver.openOutputStream(uri)?.use { out ->
                input.copyTo(out)
            }
        }
        true
    } catch (e: Exception) {
        e.printStackTrace()
        false
    } finally {
        SuFile(tmpPath).delete()
    }
}

/**
 * Restores modules from a tar at [srcUri].
 * URI is streamed into cacheDir, extracted, then the tmp file is deleted.
 */
private suspend fun restoreModulesFromUri(uri: Uri): Boolean = withContext(Dispatchers.IO) {
    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
    val tmpPath   = "${ksuApp.cacheDir}/modules_restore_$timestamp.tar"

    try {
        ksuApp.contentResolver.openInputStream(uri)?.use { input ->
            SuFile(tmpPath).newOutputStream().use { out ->
                input.copyTo(out)
            }
        }
    } catch (e: Exception) {
        e.printStackTrace()
        return@withContext false
    }

    val extractCmd = "mkdir -p '/data/adb/modules_update' && $BUSYBOX tar -xpf '$tmpPath' -C /data/adb/modules_update"
    val result = ShellUtils.fastCmdResult(extractCmd)

    SuFile(tmpPath).delete()
    return@withContext result
}

/**
 * Restores allowlist from a tar at [srcUri].
 * URI is streamed into cacheDir, extracted, then the tmp file is deleted.
 */
private suspend fun restoreAllowlistFromUri(uri: Uri): Boolean = withContext(Dispatchers.IO) {
    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
    val tmpPath   = "${ksuApp.cacheDir}/allowlist_restore_$timestamp.tar"

    try {
        ksuApp.contentResolver.openInputStream(uri)?.use { input ->
            SuFile(tmpPath).newOutputStream().use { out ->
                input.copyTo(out)
            }
        }
    } catch (e: Exception) {
        e.printStackTrace()
        return@withContext false
    }

    val extractCmd = "$BUSYBOX tar -xpf '$tmpPath' -C /data/adb/ksu"
    val result = ShellUtils.fastCmdResult(extractCmd)

    SuFile(tmpPath).delete()
    return@withContext result
}

// ─────────────────────────────────────────────────────────────────────────────

@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun BackupRestoreScreen(navigator: DestinationsNavigator) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    val snackBarHost = LocalSnackbarHost.current

    val scrollState = LocalScrollState.current
    val isNavBarHidden = (scrollState?.isScrollingDown?.value ?: false) || (scrollState?.isNavBarEnabled?.value == false)
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + if (isNavBarHidden) 0.dp else 112.dp

    Scaffold(
        topBar = {
            TopBar(
                onBack = dropUnlessResumed { navigator.popBackStack() },
                scrollBehavior = scrollBehavior
            )
        },
        snackbarHost = {
            SnackbarHost(
                hostState = snackBarHost,
                modifier = Modifier.padding(
                    bottom = navBarPadding
                )
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(
            WindowInsetsSides.Top + WindowInsetsSides.Horizontal
        )
    ) { paddingValues ->
        val loadingDialog = rememberLoadingDialog()
        val context       = LocalContext.current
        val scope         = rememberCoroutineScope()

        // Track which backup/restore type was last requested so the single
        // launcher knows what to do when the file-picker returns.
        val lastBackupType  = rememberSaveable { mutableStateOf("module") }
        val lastRestoreType = rememberSaveable { mutableStateOf("module") }
        val lastBackupName  = rememberSaveable { mutableStateOf("") }

        val backupSuccess   = stringResource(R.string.backup_success)
        val backupFailed    = stringResource(R.string.backup_failed)
        val restoreSuccess  = stringResource(R.string.restore_success)
        val restoreFailed   = stringResource(R.string.restore_failed)
        val reboot          = stringResource(R.string.reboot)

        // ── CREATE DOCUMENT launcher (backup) ────────────────────────────────
        val createBackupLauncher = rememberLauncherForActivityResult(
            contract = ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode != RESULT_OK) return@rememberLauncherForActivityResult
            val uri = result.data?.data ?: return@rememberLauncherForActivityResult
            scope.launch {
                val ok = loadingDialog.withLoading {
                    if (lastBackupType.value == "allowlist") {
                        backupAllowlistToUri(uri)
                    } else {
                        backupModulesToUri(uri)
                    }
                }
                snackBarHost.showSnackbar(
                    message = if (ok) backupSuccess else backupFailed,
                    duration = SnackbarDuration.Short
                )
            }
        }

        // ── GET CONTENT launcher (restore) ───────────────────────────────────
        val openRestoreLauncher = rememberLauncherForActivityResult(
            contract = ActivityResultContracts.StartActivityForResult()
        ) { result ->
            if (result.resultCode != RESULT_OK) return@rememberLauncherForActivityResult
            val uri = result.data?.data ?: return@rememberLauncherForActivityResult
            scope.launch {
                val ok = loadingDialog.withLoading {
                    if (lastRestoreType.value == "allowlist") {
                        restoreAllowlistFromUri(uri)
                    } else {
                        restoreModulesFromUri(uri)
                    }
                }
                if (ok && lastRestoreType.value == "module") {
                    val result = snackBarHost.showSnackbar(
                        message = restoreSuccess,
                        actionLabel = reboot,
                        duration = SnackbarDuration.Long
                    )
                    if (result == SnackbarResult.ActionPerformed) {
                        reboot()
                    }
                } else {
                    snackBarHost.showSnackbar(
                        message = if (ok) restoreSuccess else restoreFailed,
                        duration = SnackbarDuration.Short
                    )
                }
            }
        }

        // ── Content ───────────────────────────────────────────────────────────
        Column(
            modifier = Modifier
                .padding(paddingValues)
                .let { modifier ->
                    val bottomBarScrollState = LocalScrollState.current
                    val bottomBarScrollConnection = bottomBarScrollState?.let {
                        rememberScrollConnection(
                            isScrollingDown = it.isScrollingDown,
                            scrollOffset = it.scrollOffset,
                            previousScrollOffset = it.previousScrollOffset,
                            threshold = 30f
                        )
                    }
                    if (bottomBarScrollConnection != null) {
                        modifier
                            .nestedScroll(bottomBarScrollConnection)
                            .nestedScroll(scrollBehavior.nestedScrollConnection)
                    } else {
                        modifier.nestedScroll(scrollBehavior.nestedScrollConnection)
                    }
                }
                .verticalScroll(rememberScrollState())
        ) {

            // ── Module backup ─────────────────────────────────────────────────
            val moduleBackup = stringResource(R.string.module_backup)
            ListItem(
                leadingContent = { Icon(Icons.Filled.Backup, moduleBackup) },
                headlineContent = {
                    Text(
                        text = moduleBackup,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                modifier = Modifier.clickable {
                    val ts        = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
                    val suggested = "modules_backup_$ts.tar"
                    lastBackupName.value  = suggested
                    lastBackupType.value  = "module"
                    createBackupLauncher.launch(
                        Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "application/x-tar"
                            putExtra(Intent.EXTRA_TITLE, suggested)
                        }
                    )
                }
            )

            // ── Module restore ────────────────────────────────────────────────
            val moduleRestore = stringResource(R.string.module_restore)
            ListItem(
                leadingContent = {
                    Icon(
                        Icons.Filled.Restore,
                        moduleRestore,
                        tint = MaterialTheme.colorScheme.onSurface
                    )
                },
                headlineContent = {
                    Text(
                        text = moduleRestore,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                },
                modifier = Modifier.clickable {
                    lastRestoreType.value = "module"
                    openRestoreLauncher.launch(
                        Intent(Intent.ACTION_GET_CONTENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "application/x-tar"
                        }
                    )
                }
            )

            HorizontalDivider(thickness = Dp.Hairline)

            // ── Allowlist backup ──────────────────────────────────────────────
            val allowlistBackup = stringResource(R.string.allowlist_backup)
            ListItem(
                leadingContent = { Icon(Icons.Filled.Backup, allowlistBackup) },
                headlineContent = {
                    Text(
                        text = allowlistBackup,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                modifier = Modifier.clickable {
                    val ts        = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
                    val suggested = "allowlist_backup_$ts.tar"
                    lastBackupName.value  = suggested
                    lastBackupType.value  = "allowlist"
                    createBackupLauncher.launch(
                        Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "application/x-tar"
                            putExtra(Intent.EXTRA_TITLE, suggested)
                        }
                    )
                }
            )

            // ── Allowlist restore ─────────────────────────────────────────────
            val allowlistRestore = stringResource(R.string.allowlist_restore)
            ListItem(
                leadingContent = { Icon(Icons.Filled.Restore, allowlistRestore) },
                headlineContent = {
                    Text(
                        text = allowlistRestore,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                },
                modifier = Modifier.clickable {
                    lastRestoreType.value = "allowlist"
                    openRestoreLauncher.launch(
                        Intent(Intent.ACTION_GET_CONTENT).apply {
                            addCategory(Intent.CATEGORY_OPENABLE)
                            type = "application/x-tar"
                        }
                    )
                }
            )
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    onBack: () -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.backup_restore),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Black
            )
        },
        navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null)
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(
            WindowInsetsSides.Top + WindowInsetsSides.Horizontal
        ),
        scrollBehavior = scrollBehavior
    )
}

@Preview
@Composable
private fun BackupPreview() {
    BackupRestoreScreen(EmptyDestinationsNavigator)
}