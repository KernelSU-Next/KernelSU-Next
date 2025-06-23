package com.rifsxd.ksunext.ui.viewmodel

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import android.os.Parcelable
import android.os.SystemClock
import android.util.Log
import android.widget.Toast
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import com.dergoogler.mmrl.platform.Platform
import com.dergoogler.mmrl.platform.TIMEOUT_MILLIS
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.parcelize.Parcelize
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.ui.util.HanziToPinyin
import com.rifsxd.ksunext.ui.webui.getInstalledPackagesAll
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeoutOrNull
import java.text.Collator
import java.util.*
import kotlin.coroutines.resume
import kotlin.coroutines.suspendCoroutine
import androidx.core.content.edit

class SuperUserViewModel : ViewModel() {
    val isPlatformAlive get() = Platform.isAlive

    companion object {
        private const val TAG = "SuperUserViewModel"
        private var apps by mutableStateOf<List<AppInfo>>(emptyList())
        private var profileOverrides by mutableStateOf<Map<String, Natives.Profile>>(emptyMap())
    }

    @Parcelize
    data class AppInfo(
        val label: String,
        val packageInfo: PackageInfo,
        val profile: Natives.Profile?,
    ) : Parcelable {
        val packageName: String
            get() = packageInfo.packageName
        val uid: Int
            get() = packageInfo.applicationInfo!!.uid

        val allowSu: Boolean
            get() = profile != null && profile.allowSu
        val hasCustomProfile: Boolean
            get() {
                if (profile == null) {
                    return false
                }

                return if (profile.allowSu) {
                    !profile.rootUseDefault
                } else {
                    !profile.nonRootUseDefault
                }
            }
    }

    private val prefs = ksuApp.getSharedPreferences("settings", Context.MODE_PRIVATE)!!

    var search by mutableStateOf("")
    var showSystemApps by mutableStateOf(prefs.getBoolean("show_system_apps", false))
        private set
    var isRefreshing by mutableStateOf(false)
        private set

    fun updateShowSystemApps(newValue: Boolean) {
        showSystemApps = newValue
        prefs.edit { putBoolean("show_system_apps", newValue) }
    }

    private val sortedList by derivedStateOf {
        val comparator = compareBy<AppInfo> {
            when {
                it.profile != null && it.profile.allowSu -> 0
                it.profile != null && (
                    if (it.profile.allowSu) !it.profile.rootUseDefault else !it.profile.nonRootUseDefault
                ) -> 1
                else -> 2
            }
        }.then(compareBy(Collator.getInstance(Locale.getDefault()), AppInfo::label))
        apps.sortedWith(comparator).also {
            isRefreshing = false
        }
    }

    val appList by derivedStateOf {
        sortedList.map { app ->
            profileOverrides[app.packageName]?.let { app.copy(profile = it) } ?: app
        }.filter {
            it.label.contains(search, true) || it.packageName.contains(
                search,
                true
            ) || HanziToPinyin.getInstance()
                .toPinyinString(it.label).contains(search, true)
        }.filter {
            it.uid == 2000 // Always show shell
                    || showSystemApps || it.packageInfo.applicationInfo!!.flags.and(ApplicationInfo.FLAG_SYSTEM) == 0
        }
    }

    fun updateAppProfile(packageName: String, newProfile: Natives.Profile) {
        profileOverrides = profileOverrides.toMutableMap().apply {
            put(packageName, newProfile)
        }
    }

    suspend fun fetchAppList() {
        isRefreshing = true

        withContext(Dispatchers.IO) {
            withTimeoutOrNull(TIMEOUT_MILLIS) {
                while (!isPlatformAlive) {
                    delay(500)
                }
            } ?: return@withContext // Exit early if timeout

            val pm = ksuApp.packageManager
            val start = SystemClock.elapsedRealtime()

            val packages = Platform.getInstalledPackagesAll {
                Log.e(TAG, "getInstalledPackagesAll:", it)
                Toast.makeText(ksuApp, "Something went wrong, check logs", Toast.LENGTH_SHORT).show()
            }

            apps = packages.map {
                val appInfo = it.applicationInfo
                val uid = appInfo!!.uid
                val profile = Natives.getAppProfile(it.packageName, uid)
                AppInfo(
                    label = appInfo.loadLabel(pm).toString(),
                    packageInfo = it,
                    profile = profile,
                )
            }.filter { it.packageName != ksuApp.packageName }
            profileOverrides = emptyMap()
            Log.i(TAG, "load cost: ${SystemClock.elapsedRealtime() - start}")
        }
    }
}
