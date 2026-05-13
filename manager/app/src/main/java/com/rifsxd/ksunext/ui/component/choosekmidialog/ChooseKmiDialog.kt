package com.rifsxd.ksunext.ui.component.choosekmidialog

import androidx.compose.runtime.Composable
import com.rifsxd.ksunext.ui.LocalUiMode
import com.rifsxd.ksunext.ui.UiMode

@Composable
fun ChooseKmiDialog(
    show: Boolean,
    onDismissRequest: () -> Unit,
    onSelected: (String?) -> Unit
) {
    when (LocalUiMode.current) {
        UiMode.Miuix -> ChooseKmiDialogMiuix(show, onDismissRequest, onSelected)
        UiMode.Material -> ChooseKmiDialogMaterial(show, onDismissRequest, onSelected)
    }
}
