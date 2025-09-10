package com.rifsxd.ksunext.ui.viewmodel

import android.net.Uri
import android.os.SystemClock
import android.util.Log
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.ui.util.HanziToPinyin
import com.rifsxd.ksunext.ui.util.getModuleSize
import com.rifsxd.ksunext.ui.util.listModules
import com.rifsxd.ksunext.ui.util.zygiskRequired
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.text.Collator
import java.util.*

class ModuleViewModel : ViewModel() {

    companion object {
        private const val TAG = "ModuleViewModel"
        private var modules by mutableStateOf<List<ModuleInfo>>(emptyList())
    }

    class ModuleInfo(
        val id: String,
        val name: String,
        val author: String,
        val version: String,
        val versionCode: Int,
        val description: String,
        val enabled: Boolean,
        val update: Boolean,
        val remove: Boolean,
        val updateJson: String,
        val hasWebUi: Boolean,
        val hasActionScript: Boolean,
        val dirId: String,
        val size: Long,
        val banner: String,
        val zygiskRequired: Boolean
    )

    data class ModuleUpdateInfo(
        val version: String,
        val versionCode: Int,
        val zipUrl: String,
        val changelog: String,
    )

    var isRefreshing by mutableStateOf(false)
        private set

    var search by mutableStateOf("")

    var sortAToZ by mutableStateOf(false)
    var sortZToA by mutableStateOf(false)
    var sortSizeLowToHigh by mutableStateOf(false)
    var sortSizeHighToLow by mutableStateOf(false)
    var sortEnabledFirst by mutableStateOf(false)
    var sortActionFirst by mutableStateOf(false)
    var sortWebUiFirst by mutableStateOf(false)

    val moduleList by derivedStateOf {
        val comparator = when {
            sortWebUiFirst -> compareByDescending { it.hasWebUi }
            sortEnabledFirst -> compareByDescending { it.enabled }
            sortActionFirst -> compareByDescending { it.hasActionScript }
            sortAToZ -> compareBy { it.name.lowercase() }
            sortZToA -> compareByDescending { it.name.lowercase() }
            sortSizeLowToHigh -> compareBy { it.size }
            sortSizeHighToLow -> compareByDescending { it.size }
            else -> compareBy<ModuleInfo> { it.dirId }
        }.thenBy(Collator.getInstance(Locale.getDefault()), ModuleInfo::id)

        modules.filter {
            it.id.contains(search, ignoreCase = true) ||
            it.name.contains(search, ignoreCase = true) ||
            HanziToPinyin.getInstance().toPinyinString(it.name).contains(search, ignoreCase = true)
        }.sortedWith(comparator).also {
            isRefreshing = false
        }
    }


    var isNeedRefresh by mutableStateOf(false)
        private set

    fun markNeedRefresh() {
        isNeedRefresh = true
    }

    var zipUris by mutableStateOf<List<Uri>>(emptyList())

    fun updateZipUris(uris: List<Uri>) {
        zipUris = uris
    }

    fun clearZipUris() {
        zipUris = emptyList()
    }

    fun fetchModuleList() {
        
        viewModelScope.launch {

            isRefreshing = true

            withContext(Dispatchers.IO) {
                val start = SystemClock.elapsedRealtime()
                val oldModuleList = modules

                kotlin.runCatching {
                    val result = listModules()
                    Log.i(TAG, "result: $result")

                    val array = JSONArray(result)
                    modules = (0 until array.length())
                        .asSequence()
                        .map { array.getJSONObject(it) }
                        .map { obj ->
                            val id = obj.getString("id")
                            val dirId = obj.getString("dir_id")
                            val moduleDir = File("/data/adb/modules/$dirId")
                            val size = getModuleSize(moduleDir)
                            val zygiskRequired = zygiskRequired(moduleDir)

                            ModuleInfo(
                                id,
                                obj.optString("name"),
                                obj.optString("author", "Unknown"),
                                obj.optString("version", "Unknown"),
                                obj.optInt("versionCode", 0),
                                obj.optString("description"),
                                obj.getBoolean("enabled"),
                                obj.getBoolean("update"),
                                obj.getBoolean("remove"),
                                obj.optString("updateJson"),
                                obj.optBoolean("web"),
                                obj.optBoolean("action"),
                                dirId,
                                size,
                                obj.optString("banner"),
                                zygiskRequired
                            )
                        }.toList()
                    isNeedRefresh = false
                }.onFailure { e ->
                    Log.e(TAG, "fetchModuleList: ", e)
                    isRefreshing = false
                }

                // when both old and new is kotlin.collections.EmptyList
                // moduleList update will don't trigger
                if (oldModuleList === modules) {
                    isRefreshing = false
                }

                Log.i(TAG, "load cost: ${SystemClock.elapsedRealtime() - start}, modules: $modules")
            }
        }
    }

    private fun sanitizeVersionString(version: String): String {
        return version.replace(Regex("[^a-zA-Z0-9.\\-_]"), "_")
    }

    fun checkUpdate(m: ModuleInfo): Triple<String, String, String> {
        val empty = Triple("", "", "")
        if (m.updateJson.isEmpty() || m.remove || m.update || !m.enabled) {
            return empty
        }
        // download updateJson
        val result = kotlin.runCatching {
            val url = m.updateJson
            Log.i(TAG, "checkUpdate url: $url")
            val response = ksuApp.okhttpClient.newCall(
                    okhttp3.Request.Builder().url(url).build()
                ).execute()
            Log.d(TAG, "checkUpdate code: ${response.code}")
            if (response.isSuccessful) {
                response.body?.string() ?: ""
            } else {
                ""
            }
        }.getOrDefault("")
        Log.i(TAG, "checkUpdate result: $result")

        if (result.isEmpty()) {
            return empty
        }

        val updateJson = kotlin.runCatching {
            JSONObject(result)
        }.getOrNull() ?: return empty

        var version = updateJson.optString("version", "")
        version = sanitizeVersionString(version)
        val versionCode = updateJson.optInt("versionCode", 0)
        val zipUrl = updateJson.optString("zipUrl", "")
        val changelog = updateJson.optString("changelog", "")
        if (versionCode <= m.versionCode || zipUrl.isEmpty()) {
            return empty
        }

        return Triple(zipUrl, version, changelog)
    }
}
