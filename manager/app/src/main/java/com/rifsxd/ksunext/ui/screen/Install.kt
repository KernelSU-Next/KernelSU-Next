package com.rifsxd.ksunext.ui.screen

import android.app.Activity
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.LocalIndication
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import com.maxkeppeker.sheets.core.models.base.Header
import com.maxkeppeker.sheets.core.models.base.rememberUseCaseState
import com.maxkeppeler.sheets.list.ListDialog
import com.maxkeppeler.sheets.list.models.ListOption
import com.maxkeppeler.sheets.list.models.ListSelection
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.FlashScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.navigation.EmptyDestinationsNavigator
import com.rifsxd.ksunext.*
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.component.DialogHandle
import com.rifsxd.ksunext.ui.component.rememberConfirmDialog
import com.rifsxd.ksunext.ui.component.rememberCustomDialog
import com.rifsxd.ksunext.ui.util.*
import java.util.Locale

/**
 * @author weishu
 * @date 2024/3/12.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun InstallScreen(navigator: DestinationsNavigator) {

    var installMethod by remember {
        mutableStateOf<InstallMethod?>(null)
    }

    var lkmSelection by remember {
        mutableStateOf<LkmSelection>(LkmSelection.KmiNone)
    }

    val context = LocalContext.current

    val onInstall = {
        installMethod?.let { method ->
            when (method) {
                is InstallMethod.AnyKernel -> {
                    method.uri?.let { navigator.navigate(FlashScreenDestination(FlashIt.FlashAnyKernel(it))) }
                    return@let
                }
                else -> {}
            }
            if (method is InstallMethod.SelectFile && method.uri != null) {
                val name = getFileName(context, method.uri)
                val lower = name.lowercase(Locale.getDefault())
                if (lower.contains("anykernel") || lower.endsWith(".zip" ) && lower.contains("anykernel")) {
                    navigator.navigate(FlashScreenDestination(FlashIt.FlashAnyKernel(method.uri)))
                    return@let
                }
            }

            val flashIt = FlashIt.FlashBoot(
                boot = if (method is InstallMethod.SelectFile) method.uri else null,
                lkm = lkmSelection,
                ota = method is InstallMethod.DirectInstallToInactiveSlot
            )
            navigator.navigate(FlashScreenDestination(flashIt))
        }
    }

    val currentKmi by produceState(initialValue = "") { value = getCurrentKmi() }

    val selectKmiDialog = rememberSelectKmiDialog { kmi ->
        kmi?.let {
            lkmSelection = LkmSelection.KmiString(it)
            onInstall()
        }
    }

    val onClickNext = {
        if (lkmSelection == LkmSelection.KmiNone && currentKmi.isBlank()) {
            // no lkm file selected and cannot get current kmi
            selectKmiDialog.show()
        } else {
            onInstall()
        }
    }

    val selectLkmLauncher =
        rememberLauncherForActivityResult(contract = ActivityResultContracts.StartActivityForResult()) {
            if (it.resultCode == Activity.RESULT_OK) {
                it.data?.data?.let { uri ->
                    lkmSelection = LkmSelection.LkmUri(uri)
                }
            }
        }

    val selectAnyKernelLauncher =
        rememberLauncherForActivityResult(contract = ActivityResultContracts.StartActivityForResult()) {
            if (it.resultCode == Activity.RESULT_OK) {
                it.data?.data?.let { uri ->
                    // navigate to AnyKernel flasher directly
                    navigator.navigate(FlashScreenDestination(FlashIt.FlashAnyKernel(uri)))
                }
            }
        }

    val onLkmUpload = {
        selectLkmLauncher.launch(Intent(Intent.ACTION_GET_CONTENT).apply {
            type = "application/octet-stream"
        })
    }

    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())

    Scaffold(
        topBar = {
            TopBar(
                onBack = dropUnlessResumed { navigator.popBackStack() },
                onLkmUpload = onLkmUpload,
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
        ) {
            SelectInstallMethod { method ->
                installMethod = method
            }

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp)
            ) {
                (lkmSelection as? LkmSelection.LkmUri)?.let {
                    Text(
                        stringResource(
                            id = R.string.selected_lkm,
                            it.uri.lastPathSegment ?: "(file)"
                        )
                    )
                }
                Button(modifier = Modifier.fillMaxWidth(),
                    enabled = installMethod != null,
                    onClick = {
                        onClickNext()
                    }) {
                    Text(
                        stringResource(id = R.string.install_next),
                        fontSize = MaterialTheme.typography.bodyMedium.fontSize
                    )
                }
            }
        }
    }
}

sealed class InstallMethod {
    data class SelectFile(
        val uri: Uri? = null,
        @param:StringRes override val label: Int = R.string.select_file,
        override val summary: String?
    ) : InstallMethod()

    data class AnyKernel(
        val uri: Uri? = null,
        @param:StringRes override val label: Int = R.string.anykernel_install,
        override val summary: String? = null
    ) : InstallMethod()

    data object DirectInstall : InstallMethod() {
        override val label: Int
            get() = R.string.direct_install
    }

    data object DirectInstallToInactiveSlot : InstallMethod() {
        override val label: Int
            get() = R.string.install_inactive_slot
    }

    abstract val label: Int
    open val summary: String? = null
}

@Composable
private fun SelectInstallMethod(onSelected: (InstallMethod) -> Unit = {}) {
    val rootAvailable = rootAvailable()
    val isAbDevice = isAbDevice()
    val kernelVersion = getKernelVersion()
    val selectFileTip = stringResource(
        id = R.string.select_file_tip,
        if (kernelVersion.isKernel510())
            "boot"
        else
            "init_boot/vendor_boot"
    )
    val radioOptions =
        mutableListOf<InstallMethod>(InstallMethod.SelectFile(summary = selectFileTip))
    if (rootAvailable) {
        radioOptions.add(InstallMethod.DirectInstall)

        if (isAbDevice) {
            radioOptions.add(InstallMethod.DirectInstallToInactiveSlot)
        }

        radioOptions.add(InstallMethod.AnyKernel())
    }
    if (rootAvailable && !kernelVersion.isGKI()) {
        radioOptions.remove(InstallMethod.SelectFile(summary = selectFileTip))
        radioOptions.remove(InstallMethod.DirectInstall)
        if (isAbDevice) {
            radioOptions.remove(InstallMethod.DirectInstallToInactiveSlot)
        }
    }

    var selectedOption by remember { mutableStateOf<InstallMethod?>(null) }
    val selectImageLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        if (it.resultCode == Activity.RESULT_OK) {
            it.data?.data?.let { uri ->
                val option = InstallMethod.SelectFile(uri, summary = selectFileTip)
                selectedOption = option
                onSelected(option)
            }
        }
    }

    val selectAnyKernelLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) {
        if (it.resultCode == Activity.RESULT_OK) {
            it.data?.data?.let { uri ->
                val option = InstallMethod.AnyKernel(uri)
                selectedOption = option
                onSelected(option)
            }
        }
    }

    val confirmDialog = rememberConfirmDialog(onConfirm = {
        selectedOption = InstallMethod.DirectInstallToInactiveSlot
        onSelected(InstallMethod.DirectInstallToInactiveSlot)
    }, onDismiss = null)
    val dialogTitle = stringResource(id = android.R.string.dialog_alert_title)
    val dialogContent = stringResource(id = R.string.install_inactive_slot_warning)

    val onClick = { option: InstallMethod ->

        when (option) {
            is InstallMethod.SelectFile -> {
                selectImageLauncher.launch(Intent(Intent.ACTION_GET_CONTENT).apply {
                    type = "application/octet-stream"
                })
            }

            is InstallMethod.AnyKernel -> {
                selectAnyKernelLauncher.launch(Intent(Intent.ACTION_GET_CONTENT).apply {
                    type = "application/zip"
                    putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
                    addCategory(Intent.CATEGORY_OPENABLE)
                })
            }

            is InstallMethod.DirectInstall -> {
                selectedOption = option
                onSelected(option)
            }

            is InstallMethod.DirectInstallToInactiveSlot -> {
                confirmDialog.showConfirm(dialogTitle, dialogContent)
            }
        }
    }

    Column {
        radioOptions.forEach { option ->
            val interactionSource = remember { MutableInteractionSource() }
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier
                    .fillMaxWidth()
                    .toggleable(
                        value = option.javaClass == selectedOption?.javaClass,
                        onValueChange = {
                            onClick(option)
                        },
                        role = Role.RadioButton,
                        indication = LocalIndication.current,
                        interactionSource = interactionSource
                    )
            ) {
                RadioButton(
                    selected = option.javaClass == selectedOption?.javaClass,
                    onClick = {
                        onClick(option)
                    },
                    interactionSource = interactionSource
                )
                Column(
                    modifier = Modifier.padding(vertical = 12.dp)
                ) {
                    Text(
                        text = stringResource(id = option.label),
                        fontSize = MaterialTheme.typography.titleMedium.fontSize,
                        fontFamily = MaterialTheme.typography.titleMedium.fontFamily,
                        fontStyle = MaterialTheme.typography.titleMedium.fontStyle
                    )
                    option.summary?.let {
                        Text(
                            text = it,
                            fontSize = MaterialTheme.typography.bodySmall.fontSize,
                            fontFamily = MaterialTheme.typography.bodySmall.fontFamily,
                            fontStyle = MaterialTheme.typography.bodySmall.fontStyle
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun rememberSelectKmiDialog(onSelected: (String?) -> Unit): DialogHandle {
    val kernelVersion = getKernelVersion()
    return rememberCustomDialog { dismiss ->
        if (kernelVersion.isGKI()) {
            val supportedKmi by produceState(initialValue = emptyList()) {
                value = getSupportedKmis()
            }
            val options = supportedKmi.map { value ->
                ListOption(
                    titleText = value
                )
            }

            var selection by remember { mutableStateOf<String?>(null) }
            ListDialog(state = rememberUseCaseState(visible = true, onFinishedRequest = {
                onSelected(selection)
            }, onCloseRequest = {
                dismiss()
            }), header = Header.Default(
                title = stringResource(R.string.select_kmi),
            ), selection = ListSelection.Single(
                showRadioButtons = true,
                options = options,
            ) { _, option ->
                selection = option.titleText
            })
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    onBack: () -> Unit = {},
    onLkmUpload: () -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    val kernelVersion = getKernelVersion()
    TopAppBar(
        title = { Text(
                text = stringResource(R.string.install),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Black,
            ) }, navigationIcon = {
            IconButton(
                onClick = onBack
            ) { Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null) }
        }, actions = {
            if (kernelVersion.isGKI()) {
                IconButton(onClick = onLkmUpload) {
                    Icon(Icons.Filled.FileUpload, contentDescription = null)
                }
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}

@Composable
@Preview
fun SelectInstallPreview() {
    InstallScreen(EmptyDestinationsNavigator)
}