package com.rifsxd.ksunext.ui.component.uninstalldialog

import androidx.compose.runtime.Composable
import com.rifsxd.ksunext.ui.LocalUiMode
import com.rifsxd.ksunext.ui.UiMode

@Composable
fun UninstallDialog(
    show: Boolean,
    onDismissRequest: () -> Unit
) {
    when (LocalUiMode.current) {
        UiMode.Miuix -> UninstallDialogMiuix(show, onDismissRequest)
        UiMode.Material -> UninstallDialogMaterial(show, onDismissRequest)
    }
}
