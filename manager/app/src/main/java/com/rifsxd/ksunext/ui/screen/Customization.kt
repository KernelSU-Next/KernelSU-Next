package com.rifsxd.ksunext.ui.screen

import android.content.Context
import android.content.Intent
import androidx.compose.foundation.isSystemInDarkTheme
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
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.Text
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.TopAppBarScrollBehavior
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.lifecycle.compose.dropUnlessResumed
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.navigation.EmptyDestinationsNavigator
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.component.SwitchItem
import com.rifsxd.ksunext.ui.util.LocalSnackbarHost
import com.rifsxd.ksunext.ui.util.*


/**
 * @author rifsxd
 * @date 2025/6/1.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun CustomizationScreen(navigator: DestinationsNavigator) {
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
    
        Column(
            modifier = Modifier
                .padding(paddingValues)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .verticalScroll(rememberScrollState())
        ) {

            val context = LocalContext.current
            val scope = rememberCoroutineScope()

            val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

            var useBanner by rememberSaveable {
                mutableStateOf(
                    prefs.getBoolean("use_banner", true)
                )
            }
            SwitchItem(
                icon = Icons.Filled.ViewCarousel,
                title = stringResource(id = R.string.settings_banner),
                summary = stringResource(id = R.string.settings_banner_summary),
                checked = useBanner
            ) {
                prefs.edit().putBoolean("use_banner", it).apply()
                useBanner = it
            }

            var enableAmoled by rememberSaveable {
                mutableStateOf(
                    prefs.getBoolean("enable_amoled", false)
                )
            }
            var showRestartDialog by remember { mutableStateOf(false) }
            if (isSystemInDarkTheme()) {
                SwitchItem(
                    icon = Icons.Filled.Contrast,
                    title = stringResource(id = R.string.settings_amoled_mode),
                    summary = stringResource(id = R.string.settings_amoled_mode_summary),
                    checked = enableAmoled
                ) { checked ->
                    prefs.edit().putBoolean("enable_amoled", checked).apply()
                    enableAmoled = checked
                    showRestartDialog = true
                }
                if (showRestartDialog) {
                    AlertDialog(
                        onDismissRequest = { showRestartDialog = false },
                        title = { Text(stringResource(R.string.restart_required)) },
                        text = { Text(stringResource(R.string.restart_app_message)) },
                        confirmButton = {
                            TextButton(onClick = {
                                showRestartDialog = false
                                // Restart the app
                                val packageManager = context.packageManager
                                val intent = packageManager.getLaunchIntentForPackage(context.packageName)
                                intent?.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_NEW_TASK)
                                context.startActivity(intent)
                                Runtime.getRuntime().exit(0)
                            }) {
                                Text(stringResource(R.string.restart_app))
                            }
                        },
                        dismissButton = {
                            TextButton(onClick = { showRestartDialog = false }) {
                                Text(stringResource(R.string.later))
                            }
                        }
                    )
                }
            }
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
        title = { Text(stringResource(R.string.customization)) }, navigationIcon = {
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
private fun CustomizationPreview() {
    CustomizationScreen(EmptyDestinationsNavigator)
}
