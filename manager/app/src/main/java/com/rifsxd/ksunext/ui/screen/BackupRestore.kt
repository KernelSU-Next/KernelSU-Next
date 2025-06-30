package com.rifsxd.ksunext.ui.screen

import android.content.Context
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.*
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.Text
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.lifecycle.compose.dropUnlessResumed
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.navigation.EmptyDestinationsNavigator
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.component.ConfirmResult
import com.rifsxd.ksunext.ui.component.rememberConfirmDialog
import com.rifsxd.ksunext.ui.component.rememberLoadingDialog
import com.rifsxd.ksunext.ui.util.LocalSnackbarHost
import com.rifsxd.ksunext.ui.util.*

/**
 * @author rifsxd
 * @date 2025/1/14.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun BackupRestoreScreen(navigator: DestinationsNavigator) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    val snackBarHost = LocalSnackbarHost.current

    val isManager = Natives.becomeManager(ksuApp.packageName)
    val ksuVersion = if (isManager) Natives.version else null

    Scaffold(
        topBar = {
            TopBar(
                onBack = dropUnlessResumed {
                    navigator.popBackStack()
                },
                scrollBehavior = scrollBehavior
            )
        },
        snackbarHost = { SnackbarHost(snackBarHost) },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { paddingValues ->
        val loadingDialog = rememberLoadingDialog()
        val restoreDialog = rememberConfirmDialog()
        val backupDialog = rememberConfirmDialog()

        Column(
            modifier = Modifier
                .padding(paddingValues)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .verticalScroll(rememberScrollState())
        ) {

            val context = LocalContext.current
            val scope = rememberCoroutineScope()

            var showRebootDialog by remember { mutableStateOf(false) }

            if (showRebootDialog) {
                AlertDialog(
                    onDismissRequest = { showRebootDialog = false },
                    title = { Text(
                        text = stringResource(R.string.reboot_required),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.SemiBold
                    ) },
                    text = { Text(stringResource(R.string.reboot_message)) },
                    confirmButton = {
                        TextButton(onClick = {
                            showRebootDialog = false
                            reboot()
                        }) {
                            Text(stringResource(R.string.reboot))
                        }
                    },
                    dismissButton = {
                        TextButton(onClick = { showRebootDialog = false }) {
                            Text(stringResource(R.string.later))
                        }
                    }
                )
            }

            val moduleBackup = stringResource(id = R.string.module_backup)
            val backupMessage = stringResource(id = R.string.module_backup_message)
            ListItem(
                leadingContent = {
                    Icon(
                        Icons.Filled.Backup,
                        moduleBackup
                    )
                },
                headlineContent = { Text(
                    text = moduleBackup,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                ) },
                modifier = Modifier.clickable {
                    scope.launch {
                        val result = backupDialog.awaitConfirm(title = moduleBackup, content = backupMessage)
                        if (result == ConfirmResult.Confirmed) {
                            loadingDialog.withLoading {
                                moduleBackup()
                            }
                        }
                    }
                }
            )

            if (showRebootDialog) {
                AlertDialog(
                    onDismissRequest = { showRebootDialog = false },
                    title = { Text(
                        text = stringResource(R.string.reboot_required),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.SemiBold
                    ) },
                    text = { Text(stringResource(R.string.reboot_message)) },
                    confirmButton = {
                        TextButton(onClick = {
                            showRebootDialog = false
                            reboot()
                        }) {
                            Text(stringResource(R.string.reboot))
                        }
                    },
                    dismissButton = {
                        TextButton(onClick = { showRebootDialog = false }) {
                            Text(stringResource(R.string.later))
                        }
                    }
                )
            }

            val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

            var useOverlayFs by rememberSaveable {
                mutableStateOf(readMountSystemFile())
            }

            val moduleRestore = stringResource(id = R.string.module_restore)
            val restoreMessage = stringResource(id = R.string.module_restore_message)

            ListItem(
                leadingContent = {
                    Icon(
                        Icons.Filled.Restore,
                        moduleRestore,
                        tint = if (useOverlayFs) MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f) else MaterialTheme.colorScheme.onSurface
                    )
                },
                headlineContent = { 
                    Text(
                        moduleRestore,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        color = if (useOverlayFs) MaterialTheme.colorScheme.onSurface.copy(alpha = 0.38f) else MaterialTheme.colorScheme.onSurface
                    ) 
                },
                modifier = Modifier.clickable(
                    enabled = !useOverlayFs,
                    onClick = {
                        scope.launch {
                            val result = restoreDialog.awaitConfirm(title = moduleRestore, content = restoreMessage)
                            if (result == ConfirmResult.Confirmed) {
                                loadingDialog.withLoading {
                                    moduleRestore()
                                    showRebootDialog = true
                                }
                            }
                        }
                    }
                )
            )

            HorizontalDivider(thickness = Dp.Hairline)

            val allowlistBackup = stringResource(id = R.string.allowlist_backup)
            val allowlistbackupMessage = stringResource(id = R.string.allowlist_backup_message)
            ListItem(
                leadingContent = {
                    Icon(
                        Icons.Filled.Backup,
                        allowlistBackup
                    )
                },
                headlineContent = { Text(
                    text = allowlistBackup,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                ) },
                modifier = Modifier.clickable {
                    scope.launch {
                        val result = backupDialog.awaitConfirm(title = allowlistBackup, content = allowlistbackupMessage)
                        if (result == ConfirmResult.Confirmed) {
                            loadingDialog.withLoading {
                                allowlistBackup()
                            }
                        }
                    }
                }
            )

            val allowlistRestore = stringResource(id = R.string.allowlist_restore)
            val allowlistrestoreMessage = stringResource(id = R.string.allowlist_restore_message)
            ListItem(
                leadingContent = {
                    Icon(
                        Icons.Filled.Restore,
                        allowlistRestore
                    )
                },
                headlineContent = { Text(
                    text = allowlistRestore,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                ) },
                modifier = Modifier.clickable {
                    scope.launch {
                        val result = restoreDialog.awaitConfirm(title = allowlistRestore, content = allowlistrestoreMessage)
                        if (result == ConfirmResult.Confirmed) {
                            loadingDialog.withLoading {
                                allowlistRestore()
                            }
                        }
                    }
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
        title = { Text(
                text = stringResource(R.string.backup_restore),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Black,
            ) }, navigationIcon = {
            IconButton(
                onClick = onBack
            ) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null) }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}

@Preview
@Composable
private fun BackupPreview() {
    BackupRestoreScreen(EmptyDestinationsNavigator)
}
