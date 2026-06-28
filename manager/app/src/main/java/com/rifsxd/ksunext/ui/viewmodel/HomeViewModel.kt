package com.rifsxd.ksunext.ui.viewmodel

import android.content.Context
import android.os.Build
import android.system.Os
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.rifsxd.ksunext.BuildConfig
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.getKernelVersion
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.ui.screen.home.HomeUiState
import com.rifsxd.ksunext.ui.screen.home.SystemInfo
import com.rifsxd.ksunext.ui.screen.home.getManagerVersion
import com.rifsxd.ksunext.ui.util.checkNewVersion
import com.rifsxd.ksunext.ui.util.getMetaModuleStatus
import com.rifsxd.ksunext.ui.util.getModuleCount
import com.rifsxd.ksunext.ui.util.getSELinuxStatusRaw
import com.rifsxd.ksunext.ui.util.getSuperuserCount
import com.rifsxd.ksunext.ui.util.KsuCli
import com.rifsxd.ksunext.ui.util.module.LatestVersionInfo
import com.rifsxd.ksunext.ui.util.rootAvailable

class HomeViewModel : ViewModel() {

    private val _uiState = MutableStateFlow(buildState())
    val uiState: StateFlow<HomeUiState> = _uiState.asStateFlow()

    fun refresh() {
        viewModelScope.launch {
            val baseState = withContext(Dispatchers.IO) { buildState() }
            _uiState.update { baseState }
            if (baseState.checkUpdateEnabled) {
                val latestVersionInfo = withContext(Dispatchers.IO) { checkNewVersion() }
                _uiState.update { it.copy(latestVersionInfo = latestVersionInfo) }
            }
        }
    }

    private fun buildState(): HomeUiState {
        val kernelVersion = getKernelVersion()
        val isManager = Natives.isManager
        val ksuVersionTag = if (isManager) Natives.getVersionTag() else null
        val ksuVersion = if (isManager) Natives.version else null
        val lkmMode = ksuVersion?.let { if (kernelVersion.isGKI()) Natives.isLkmMode else null }
        val isRootAvailable = rootAvailable()
        val managerVersion = getManagerVersion(ksuApp)
        val hookMode = if (isManager) Natives.getHookMode() else null
        val zygiskEnabled = if (isManager) Natives.isZygiskEnabled() else false
        val metaModuleStatus = if (isManager) getMetaModuleStatus() else null

        return HomeUiState(
            kernelVersion = kernelVersion,
            ksuVersionTag = ksuVersionTag,
            ksuVersion = ksuVersion,
            lkmMode = lkmMode,
            isManager = isManager,
            isManagerPrBuild = BuildConfig.IS_PR_BUILD,
            isKernelPrBuild = Natives.isPrBuild,
            requiresNewKernel = isManager && Natives.requireNewKernel(),
            isRootAvailable = isRootAvailable,
            isSafeMode = Natives.isSafeMode,
            isLateLoadMode = Natives.isLateLoadMode,
            checkUpdateEnabled = ksuApp.getSharedPreferences("settings", Context.MODE_PRIVATE)
                .getBoolean("check_update", true),
            latestVersionInfo = LatestVersionInfo(),
            currentManagerVersionCode = managerVersion.versionCode,
            superuserCount = getSuperuserCount(),
            moduleCount = getModuleCount(),
            systemInfo = SystemInfo(
                kernelVersion = Os.uname().release,
                managerVersion = "${managerVersion.versionName} (${managerVersion.versionCode})",
                fingerprint = Build.FINGERPRINT,
                selinuxStatus = getSELinuxStatusRaw(),
                seccompStatus = runCatching {
                    Os.prctl(21 /* PR_GET_SECCOMP */, 0, 0, 0, 0)
                }.getOrDefault(-1),
            ),
            hookMode = hookMode,
            zygiskEnabled = zygiskEnabled,
            metaModuleStatus = metaModuleStatus,
        )
    }
}
