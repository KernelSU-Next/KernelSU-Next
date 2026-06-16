package com.rifsxd.ksunext.ui.screen

import android.content.Context
import android.content.SharedPreferences
import android.net.Uri
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import coil.compose.AsyncImage
import coil.request.ImageRequest
import com.rifsxd.ksunext.ui.LocalScrollState
import com.rifsxd.ksunext.ui.rememberScrollConnection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.FlashScreenDestination
import com.ramcosta.composedestinations.generated.destinations.MetaModuleScreenDestination
import com.ramcosta.composedestinations.navigation.DestinationsNavigator
import com.ramcosta.composedestinations.navigation.EmptyDestinationsNavigator
import com.rifsxd.ksunext.BuildConfig
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.util.LocalSnackbarHost
import com.rifsxd.ksunext.ui.screen.FlashIt
import com.rifsxd.ksunext.ui.util.download
import com.rifsxd.ksunext.ui.util.DownloadListener
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import com.dergoogler.mmrl.ui.component.LabelItem
import com.dergoogler.mmrl.ui.component.LabelItemDefaults
import com.rifsxd.ksunext.ui.component.SearchAppBar
import org.json.JSONArray
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.URL
import android.content.Intent

data class ModuleRepo(
    val name: String,
    val description: String,
    val author: String,
    val repoUrl: String,
    val bannerLink: String = "",
    val license: String = "",
    val visibility: Int = 1,
    val latestVersion: String = "",
    val downloadUrl: String = "",
    val stars: Int = -1,
    val isLoading: Boolean = true
)

sealed class ModuleRepoState {
    object Loading : ModuleRepoState()
    data class Success(val modules: List<ModuleRepo>) : ModuleRepoState()
    data class Error(val message: String) : ModuleRepoState()
}

// SharedPreferences helper functions
private const val PREFS_NAME = "module_repo_prefs"
private const val KEY_JSON_URLS = "json_urls"
private const val KEY_NON_FREE_ENABLED = "non_free_enabled"
private const val DEFAULT_JSON_URL = "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next-Modules-Repo/refs/heads/main/modules.json"
private const val NON_FREE_JSON_URL = "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next-Modules-Repo/refs/heads/main/non_free_modules.json"
private const val URL_SEPARATOR = "|||"

private fun getModuleRepoPrefs(context: Context): SharedPreferences {
    return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}

private fun saveJsonUrls(context: Context, urls: List<String>) {
    val joined = urls.joinToString(URL_SEPARATOR)
    getModuleRepoPrefs(context).edit().putString(KEY_JSON_URLS, joined).apply()
}

private fun loadJsonUrls(context: Context): List<String> {
    val raw = getModuleRepoPrefs(context).getString(KEY_JSON_URLS, null)
    return if (raw.isNullOrBlank()) {
        listOf(DEFAULT_JSON_URL)
    } else {
        raw.split(URL_SEPARATOR).filter { it.isNotBlank() }
    }
}

private fun saveNonFreeEnabled(context: Context, enabled: Boolean) {
    getModuleRepoPrefs(context).edit().putBoolean(KEY_NON_FREE_ENABLED, enabled).apply()
}

private fun loadNonFreeEnabled(context: Context): Boolean {
    return getModuleRepoPrefs(context).getBoolean(KEY_NON_FREE_ENABLED, false)
}

/**
 * @author rifsxd
 * @date 2026/1/29.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun ModuleRepoScreen(navigator: DestinationsNavigator) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    val bottomBarScrollState = LocalScrollState.current
    val bottomBarScrollConnection = if (bottomBarScrollState != null) {
        rememberScrollConnection(
            isScrollingDown = bottomBarScrollState.isScrollingDown,
            scrollOffset = bottomBarScrollState.scrollOffset,
            previousScrollOffset = bottomBarScrollState.previousScrollOffset,
            threshold = 30f
        )
    } else null
    val snackBarHost = LocalSnackbarHost.current
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val isManager = Natives.isManager
    val ksuVersion = if (isManager) Natives.version else null
    val scrollState = LocalScrollState.current
    val isNavBarHidden = scrollState?.isScrollingDown?.value ?: false
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + if (isNavBarHidden) 0.dp else 112.dp

    var jsonUrls by remember { mutableStateOf(loadJsonUrls(context)) }
    var nonFreeEnabled by remember { mutableStateOf(loadNonFreeEnabled(context)) }

    // Repo manager dialog
    var showRepoManagerDialog by remember { mutableStateOf(false) }
    // Add/edit URL dialog — editingIndex == null means adding new
    var showAddEditDialog by remember { mutableStateOf(false) }
    var editingIndex by remember { mutableStateOf<Int?>(null) }
    var pendingUrl by remember { mutableStateOf("") }

    var searchText by remember { mutableStateOf("") }
    var moduleState by remember { mutableStateOf<ModuleRepoState>(ModuleRepoState.Loading) }
    var downloadingModuleName by remember { mutableStateOf<String?>(null) }
    var downloadedUri by remember { mutableStateOf<Uri?>(null) }
    val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
    var sortAToZ by remember { mutableStateOf(prefs.getBoolean("module_sort_a_to_z", true)) }
    var sortZToA by remember { mutableStateOf(prefs.getBoolean("module_sort_z_to_a", false)) }
    var sortStarsLowToHigh by remember { mutableStateOf(prefs.getBoolean("module_sort_stars_low_to_high", false)) }
    var sortStarsHighToLow by remember { mutableStateOf(prefs.getBoolean("module_sort_stars_high_to_low", false)) }

    DownloadListener(context) { uri ->
        downloadedUri = uri
        downloadingModuleName = null
        navigator.navigate(
            FlashScreenDestination(FlashIt.FlashModules(listOf(uri)))
        )
    }

    suspend fun fetchModuleReposFromJson(jsonUrl: String): List<ModuleRepo> {
        return withContext(Dispatchers.IO) {
            try {
                val conn = URL(jsonUrl).openConnection() as java.net.HttpURLConnection
                conn.setRequestProperty("User-Agent", "KernelSU-Next/${BuildConfig.VERSION_CODE}")
                val text = BufferedReader(InputStreamReader(conn.inputStream)).use { it.readText() }
                conn.disconnect()

                val arr = JSONArray(text)
                val out = mutableListOf<ModuleRepo>()
                for (i in 0 until arr.length()) {
                    val obj = arr.getJSONObject(i)
                    out += ModuleRepo(
                        name = obj.optString("name"),
                        description = obj.optString("description"),
                        author = obj.optString("author"),
                        repoUrl = obj.optString("repoUrl"),
                        bannerLink = obj.optString("bannerUrl", ""),
                        license = obj.optString("license", ""),
                        visibility = obj.optInt("visibility", 1)
                    )
                }
                out
            } catch (e: Exception) {
                e.printStackTrace()
                emptyList()
            }
        }
    }

    suspend fun loadModules() {
        try {
            moduleState = ModuleRepoState.Loading

            // Fetch all repo JSONs in parallel then merge
            val effectiveUrls = if (nonFreeEnabled) jsonUrls + NON_FREE_JSON_URL else jsonUrls
            val allModules = withContext(Dispatchers.IO) {
                effectiveUrls.map { url -> async { fetchModuleReposFromJson(url) } }
                    .awaitAll()
                    .flatten()
            }

            // Deduplicate by (name + repoUrl), keep first occurrence
            val seen = mutableSetOf<String>()
            val visibleModules = allModules
                .filter { it.visibility == 1 }
                .filter { module -> seen.add("${module.name}||${module.repoUrl}") }

            if (visibleModules.isEmpty()) {
                moduleState = ModuleRepoState.Error("No modules found across all repositories")
                return
            }

            moduleState = ModuleRepoState.Success(
                visibleModules.map { it.copy(latestVersion = "", downloadUrl = "", stars = -1, isLoading = true) }
            )

            kotlinx.coroutines.coroutineScope {
                visibleModules.forEach { baseModule ->
                    launch {
                        try {
                            val releaseInfo = withContext(Dispatchers.IO) { fetchLatestReleaseInfo(baseModule.repoUrl) }
                            val updated = baseModule.copy(
                                latestVersion = releaseInfo?.version ?: "null",
                                downloadUrl = releaseInfo?.zipUrl ?: "",
                                stars = releaseInfo?.stars ?: -1,
                                isLoading = false
                            )
                            val current = moduleState
                            if (current is ModuleRepoState.Success) {
                                val mutable = current.modules.toMutableList()
                                val idx = mutable.indexOfFirst { it.name == baseModule.name && it.repoUrl == baseModule.repoUrl }
                                if (idx >= 0) mutable[idx] = updated else mutable.add(updated)
                                moduleState = ModuleRepoState.Success(mutable)
                            }
                        } catch (e: Exception) {
                            val updated = baseModule.copy(latestVersion = "null", downloadUrl = "", stars = -1, isLoading = false)
                            val current = moduleState
                            if (current is ModuleRepoState.Success) {
                                val mutable = current.modules.toMutableList()
                                val idx = mutable.indexOfFirst { it.name == baseModule.name && it.repoUrl == baseModule.repoUrl }
                                if (idx >= 0) mutable[idx] = updated else mutable.add(updated)
                                moduleState = ModuleRepoState.Success(mutable)
                            }
                        }
                    }
                }
            }
        } catch (e: Exception) {
            moduleState = ModuleRepoState.Error(e.message ?: "Unknown error")
        }
    }

    LaunchedEffect(jsonUrls, nonFreeEnabled) {
        scope.launch { loadModules() }
    }

    if (showRepoManagerDialog) {
        AlertDialog(
            onDismissRequest = { showRepoManagerDialog = false },
            title = { Text(stringResource(R.string.manage_repositories)) },
            text = {
                Column(modifier = Modifier.fillMaxWidth()) {
                    // Non-free repo toggle
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(bottom = 4.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.non_free_repo),
                                style = MaterialTheme.typography.bodyMedium,
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                text = stringResource(R.string.non_free_repo_summary),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        Switch(
                            checked = nonFreeEnabled,
                            onCheckedChange = { enabled ->
                                nonFreeEnabled = enabled
                                saveNonFreeEnabled(context, enabled)
                            }
                        )
                    }

                    HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))

                    if (jsonUrls.isEmpty()) {
                        Text(
                            text = stringResource(R.string.no_repositories_configured),
                            style = MaterialTheme.typography.bodyMedium,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )
                    } else {
                        jsonUrls.forEachIndexed { index, url ->
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .padding(vertical = 4.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(
                                    text = url,
                                    style = MaterialTheme.typography.bodySmall,
                                    modifier = Modifier.weight(1f),
                                    maxLines = 2,
                                    overflow = TextOverflow.Ellipsis
                                )
                                IconButton(
                                    onClick = {
                                        editingIndex = index
                                        pendingUrl = url
                                        showAddEditDialog = true
                                    }
                                ) {
                                    Icon(Icons.Default.Edit, contentDescription = "Edit")
                                }
                                // Prevent deletion of the last remaining repo
                                IconButton(
                                    onClick = {
                                        val updated = jsonUrls.toMutableList().also { it.removeAt(index) }
                                        jsonUrls = updated
                                        saveJsonUrls(context, updated)
                                    },
                                    enabled = jsonUrls.size > 1
                                ) {
                                    Icon(Icons.Default.Delete, contentDescription = "Remove")
                                }
                            }
                            if (index < jsonUrls.lastIndex) {
                                HorizontalDivider()
                            }
                        }
                    }

                    Spacer(modifier = Modifier.height(8.dp))

                    OutlinedButton(
                        onClick = {
                            editingIndex = null
                            pendingUrl = ""
                            showAddEditDialog = true
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Default.Add, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(modifier = Modifier.width(6.dp))
                        Text(stringResource(R.string.add_repository))
                    }

                    TextButton(
                        onClick = {
                            jsonUrls = listOf(DEFAULT_JSON_URL)
                            saveJsonUrls(context, listOf(DEFAULT_JSON_URL))
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.reset_to_default))
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = { showRepoManagerDialog = false }) {
                    Text(stringResource(R.string.close))
                }
            }
        )
    }

    if (showAddEditDialog) {
        AlertDialog(
            onDismissRequest = {
                showAddEditDialog = false
                pendingUrl = ""
            },
            title = { Text(if (editingIndex == null) "Add Repository" else "Edit Repository") },
            text = {
                Column {
                    Text(
                        text = "Enter the module repository JSON URL:",
                        style = MaterialTheme.typography.bodyMedium,
                        modifier = Modifier.padding(bottom = 8.dp)
                    )
                    OutlinedTextField(
                        value = pendingUrl,
                        onValueChange = { pendingUrl = it },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = false,
                        maxLines = 3,
                        placeholder = { Text("https://...") }
                    )
                }
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        val trimmed = pendingUrl.trim()
                        if (trimmed.isNotBlank()) {
                            val updated = jsonUrls.toMutableList()
                            val idx = editingIndex
                            if (idx == null) updated.add(trimmed) else updated[idx] = trimmed
                            jsonUrls = updated
                            saveJsonUrls(context, updated)
                            showAddEditDialog = false
                            pendingUrl = ""
                        }
                    }
                ) {
                    Text(if (editingIndex == null) "Add" else "Apply")
                }
            },
            dismissButton = {
                TextButton(
                    onClick = {
                        showAddEditDialog = false
                        pendingUrl = ""
                    }
                ) {
                    Text("Cancel")
                }
            }
        )
    }

    Scaffold(
        topBar = {
            SearchAppBar(
                title = {
                    Text(
                        text = stringResource(R.string.module_repo_screen),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Black,
                    )
                },
                searchText = searchText,
                onSearchTextChange = { searchText = it },
                onClearClick = { searchText = "" },
                onBackClick = dropUnlessResumed { navigator.popBackStack() },
                actionsContent = {
                    IconButton(onClick = { showRepoManagerDialog = true }) {
                        Icon(
                            imageVector = Icons.Filled.Edit,
                            contentDescription = "Manage Repositories"
                        )
                    }
                    IconButton(onClick = { navigator.navigate(MetaModuleScreenDestination) }) {
                        Icon(
                            imageVector = Icons.Filled.SettingsSuggest,
                            contentDescription = stringResource(id = R.string.module_repo_screen)
                        )
                    }
                },
                dropdownContent = {
                    var showDropdown by remember { mutableStateOf(false) }
                    IconButton(
                        onClick = { showDropdown = true },
                    ) {
                        Icon(
                            imageVector = Icons.Filled.MoreVert,
                            contentDescription = stringResource(id = R.string.settings)
                        )
                        DropdownMenu(
                            expanded = showDropdown,
                            onDismissRequest = {
                                showDropdown = false
                            }
                        ) {
                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_a_to_z))
                                },
                                trailingIcon = {
                                    Checkbox(checked = sortAToZ, onCheckedChange = null)
                                },
                                onClick = {
                                    sortAToZ = !sortAToZ
                                    sortZToA = false
                                    sortStarsLowToHigh = false
                                    sortStarsHighToLow = false
                                    prefs.edit().putBoolean("module_sort_a_to_z", sortAToZ).apply()
                                    prefs.edit().putBoolean("module_sort_z_to_a", false).apply()
                                    prefs.edit().putBoolean("module_sort_stars_low_to_high", false).apply()
                                    prefs.edit().putBoolean("module_sort_stars_high_to_low", false).apply()
                                }
                            )

                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_z_to_a))
                                },
                                trailingIcon = {
                                    Checkbox(checked = sortZToA, onCheckedChange = null)
                                },
                                onClick = {
                                    sortZToA = !sortZToA
                                    sortAToZ = false
                                    sortStarsLowToHigh = false
                                    sortStarsHighToLow = false
                                    prefs.edit().putBoolean("module_sort_z_to_a", sortZToA).apply()
                                    prefs.edit().putBoolean("module_sort_a_to_z", false).apply()
                                    prefs.edit().putBoolean("module_sort_stars_low_to_high", false).apply()
                                    prefs.edit().putBoolean("module_sort_stars_high_to_low", false).apply()
                                }
                            )
                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_stars_low_to_high))
                                },
                                trailingIcon = {
                                    Checkbox(checked = sortStarsLowToHigh, onCheckedChange = null)
                                },
                                onClick = {
                                    sortStarsLowToHigh = !sortStarsLowToHigh
                                    sortStarsHighToLow = false
                                    sortAToZ = false
                                    sortZToA = false
                                    prefs.edit().putBoolean("module_sort_stars_low_to_high", sortStarsLowToHigh).apply()
                                    prefs.edit().putBoolean("module_sort_stars_high_to_low", false).apply()
                                    prefs.edit().putBoolean("module_sort_a_to_z", false).apply()
                                    prefs.edit().putBoolean("module_sort_z_to_a", false).apply()
                                }
                            )

                            DropdownMenuItem(
                                text = {
                                    Text(stringResource(R.string.module_sort_stars_high_to_low))
                                },
                                trailingIcon = {
                                    Checkbox(checked = sortStarsHighToLow, onCheckedChange = null)
                                },
                                onClick = {
                                    sortStarsHighToLow = !sortStarsHighToLow
                                    sortStarsLowToHigh = false
                                    sortAToZ = false
                                    sortZToA = false
                                    prefs.edit().putBoolean("module_sort_stars_high_to_low", sortStarsHighToLow).apply()
                                    prefs.edit().putBoolean("module_sort_stars_low_to_high", false).apply()
                                    prefs.edit().putBoolean("module_sort_a_to_z", false).apply()
                                    prefs.edit().putBoolean("module_sort_z_to_a", false).apply()
                                }
                            )
                        }
                    }
                },
                scrollBehavior = scrollBehavior
            )
        },
        snackbarHost = { SnackbarHost(snackBarHost, modifier = Modifier.padding(bottom = navBarPadding)) },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { paddingValues ->
        when (val state = moduleState) {
            is ModuleRepoState.Loading -> {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator()
                }
            }
            is ModuleRepoState.Success -> {
                val filteredModules = if (searchText.isBlank()) {
                    state.modules
                } else {
                    state.modules.filter { module ->
                        module.name.contains(searchText, ignoreCase = true) ||
                        module.description.contains(searchText, ignoreCase = true) ||
                        module.author.contains(searchText, ignoreCase = true)
                    }
                }

                val sortedModules = when {
                    sortAToZ -> filteredModules.sortedBy { it.name.lowercase() }
                    sortZToA -> filteredModules.sortedByDescending { it.name.lowercase() }
                    sortStarsLowToHigh -> filteredModules.sortedWith(compareBy { if (it.stars < 0) Int.MAX_VALUE else it.stars })
                    sortStarsHighToLow -> filteredModules.sortedWith(compareByDescending { if (it.stars < 0) Int.MIN_VALUE else it.stars })
                    else -> filteredModules
                }

                val isRefreshing = moduleState is ModuleRepoState.Loading

                PullToRefreshBox(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues),
                    isRefreshing = isRefreshing,
                    onRefresh = {
                        scope.launch {
                            moduleState = ModuleRepoState.Loading
                            loadModules()
                        }
                    }
                ) {
                    LazyColumn(
                        modifier = Modifier
                            .fillMaxSize()
                            .let { modifier ->
                                if (bottomBarScrollConnection != null) {
                                    modifier
                                        .nestedScroll(bottomBarScrollConnection)
                                        .nestedScroll(scrollBehavior.nestedScrollConnection)
                                } else {
                                    modifier.nestedScroll(scrollBehavior.nestedScrollConnection)
                                }
                            },
                        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 16.dp + navBarPadding),
                        verticalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        items(sortedModules) { module ->
                            val isThisModuleDownloading = downloadingModuleName == module.name

                            val isInstallEnabled = when {
                                downloadingModuleName != null && !isThisModuleDownloading -> false
                                else -> true
                            }

                            ModuleRepoCard(
                                module = module,
                                isInstallEnabled = isInstallEnabled,
                                isDownloading = isThisModuleDownloading,
                                onCardClick = {
                                    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(module.repoUrl))
                                    context.startActivity(intent)
                                },
                                onDownload = { selectedModule ->
                                    downloadingModuleName = selectedModule.name
                                    scope.launch {
                                        try {
                                            val fileName = "${selectedModule.name.replace(" ", "_")}_${selectedModule.latestVersion.replace("/", "_")}.zip"
                                            download(
                                                context,
                                                selectedModule.downloadUrl,
                                                fileName,
                                                "Downloading ${selectedModule.name}"
                                            )
                                        } catch (e: Exception) {
                                            snackBarHost.showSnackbar("Error downloading module: ${e.message}")
                                            downloadingModuleName = null
                                        }
                                    }
                                }
                            )
                        }
                    }
                }
            }
            is ModuleRepoState.Error -> {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues),
                    contentAlignment = Alignment.Center
                ) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier.padding(16.dp)
                    ) {
                        Text(
                            text = stringResource(R.string.error_loading_modules),
                            style = MaterialTheme.typography.titleMedium
                        )
                        Button(
                            onClick = {
                                scope.launch {
                                    moduleState = ModuleRepoState.Loading
                                    loadModules()
                                }
                            },
                            modifier = Modifier.padding(top = 16.dp)
                        ) {
                            Text(stringResource(R.string.retry))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ModuleRepoCard(
    module: ModuleRepo,
    isInstallEnabled: Boolean = true,
    isDownloading: Boolean = false,
    onCardClick: () -> Unit,
    onDownload: (ModuleRepo) -> Unit,
    modifier: Modifier = Modifier
) {
    Card(
        modifier = modifier
            .fillMaxWidth()
            .clickable { onCardClick() }
    ) {
        Box(modifier = Modifier.fillMaxWidth()) {
            val context = LocalContext.current
            val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            val useBanner = prefs.getBoolean("use_banner", true)

            if (useBanner && module.bannerLink.isNotEmpty()) {
                val isDark = isSystemInDarkTheme()
                val colorScheme = MaterialTheme.colorScheme
                val amoledMode = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
                    .getBoolean("amoled_mode", false)
                val isDynamic = colorScheme.primary != colorScheme.secondary

                val fadeColor = when {
                    amoledMode && isDark -> Color.Black
                    isDynamic -> colorScheme.surface
                    isDark -> Color(0xFF222222)
                    else -> Color.White
                }

                Box(
                    modifier = Modifier
                        .matchParentSize(),
                    contentAlignment = Alignment.Center
                ) {
                    if (module.bannerLink.startsWith("http", true)) {
                        AsyncImage(
                            model = module.bannerLink,
                            contentDescription = null,
                            modifier = Modifier
                                .fillMaxWidth()
                                .fillMaxHeight(),
                            contentScale = ContentScale.Crop,
                            alpha = 0.18f
                        )
                    }

                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .fillMaxHeight()
                            .background(
                                Brush.verticalGradient(
                                    colors = listOf(
                                        fadeColor.copy(alpha = 0.0f),
                                        fadeColor.copy(alpha = 0.8f)
                                    ),
                                    startY = 0f,
                                    endY = Float.POSITIVE_INFINITY
                                )
                            )
                    )
                }
            }

            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(22.dp, 18.dp, 22.dp, 12.dp)
            ) {
                val hasLicense = module.license.isNotEmpty()
                val hasStars = !module.isLoading && module.stars >= 0

                if (hasLicense || hasStars) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        if (hasLicense) {
                            LabelItem(
                                icon = {
                                    Icon(
                                        tint = LabelItemDefaults.style.contentColor,
                                        imageVector = Icons.Filled.LocalOffer,
                                        contentDescription = null
                                    )
                                },
                                text = {
                                    Text(
                                        text = module.license,
                                        style = LabelItemDefaults.style.textStyle.copy(
                                            color = LabelItemDefaults.style.contentColor,
                                        )
                                    )
                                }
                            )
                        }
                        if (hasStars) {
                            val starsText = when {
                                module.stars >= 1_000 -> "${"%.1f".format(module.stars / 1000.0)}k"
                                else -> "${module.stars}"
                            }
                            LabelItem(
                                icon = {
                                    Icon(
                                        imageVector = Icons.Filled.Star,
                                        tint = MaterialTheme.colorScheme.onTertiaryContainer,
                                        contentDescription = null
                                    )
                                },
                                text = {
                                    Text(
                                        text = starsText,
                                        style = LabelItemDefaults.style.textStyle.copy(
                                            color = MaterialTheme.colorScheme.onTertiaryContainer,
                                        )
                                    )
                                },
                                style = LabelItemDefaults.style.copy(
                                    containerColor = MaterialTheme.colorScheme.tertiaryContainer
                                )
                            )
                        }
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                }

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = module.name,
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = "by ${module.author}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                }

                Text(
                    text = module.description,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = 8.dp)
                )

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = stringResource(R.string.latest_version),
                            style = MaterialTheme.typography.labelSmall
                        )
                        Text(
                            text = if (module.latestVersion.isNotEmpty()) module.latestVersion else stringResource(R.string.loading),
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.SemiBold
                        )
                    }

                    Button(
                        onClick = { onDownload(module) },
                        modifier = Modifier.defaultMinSize(52.dp, 32.dp),
                        enabled = module.downloadUrl.isNotEmpty() && isInstallEnabled,
                        shape = ButtonDefaults.textShape,
                        contentPadding = ButtonDefaults.TextButtonContentPadding
                    ) {
                        if (isDownloading) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(18.dp),
                                strokeWidth = 2.dp,
                                color = MaterialTheme.colorScheme.onPrimary
                            )
                        } else {
                            Icon(
                                imageVector = Icons.Default.CloudDownload,
                                contentDescription = "Download",
                                modifier = Modifier.size(20.dp)
                            )
                        }
                        Spacer(modifier = Modifier.width(7.dp))
                        Text(
                            modifier = Modifier.padding(start = 0.dp),
                            text = stringResource(R.string.install),
                            fontFamily = MaterialTheme.typography.labelMedium.fontFamily,
                            fontSize = MaterialTheme.typography.labelMedium.fontSize
                        )
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun TopBar(
    onBack: () -> Unit = {},
    onMetaModuleClick: () -> Unit = {},
    onManageRepos: () -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.module_repo_screen),
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.Black,
            )
        },
        navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = null)
            }
        },
        actions = {
            IconButton(onClick = onManageRepos) {
                Icon(Icons.Filled.Edit, contentDescription = "Manage Repositories")
            }
            IconButton(onClick = onMetaModuleClick) {
                Icon(Icons.Filled.SettingsSuggest, contentDescription = stringResource(id = R.string.module_repo_screen))
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}

private suspend fun fetchLatestReleaseInfo(repoUrl: String): ReleaseInfoModuleRepo? {
    return withContext(Dispatchers.IO) {
        try {
            val stars = try {
                val htmlConn = URL(repoUrl).openConnection() as java.net.HttpURLConnection
                htmlConn.setRequestProperty("User-Agent", "KernelSU-Next/${BuildConfig.VERSION_CODE}")
                htmlConn.setRequestProperty("Accept", "text/html")
                val html = BufferedReader(InputStreamReader(htmlConn.inputStream)).use { it.readText() }
                htmlConn.disconnect()
                val titleMatch = """id="repo-stars-counter-star"[^>]*title="([^"]+)"""".toRegex()
                    .find(html)?.groupValues?.get(1)
                titleMatch?.replace(",", "")?.trim()?.toIntOrNull() ?: -1
            } catch (e: Exception) {
                e.printStackTrace()
                -1
            }

            val latestUrl = "$repoUrl/releases/latest"
            val connection = URL(latestUrl).openConnection() as java.net.HttpURLConnection
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("User-Agent", "KernelSU-Next/${BuildConfig.VERSION_CODE}")

            val redirectUrl = connection.getHeaderField("Location")
            connection.disconnect()

            if (redirectUrl == null) return@withContext ReleaseInfoModuleRepo(
                version = "null",
                zipUrl = "",
                stars = stars
            )

            val tagMatch = """/tag/([^/?\s]+)""".toRegex()
                .find(redirectUrl)?.groupValues?.get(1)
                ?: return@withContext ReleaseInfoModuleRepo(version = "null", zipUrl = "", stars = stars)

            val releasePageUrl = "$repoUrl/releases/expanded_assets/$tagMatch"
            val pageConnection = URL(releasePageUrl).openConnection()
            pageConnection.setRequestProperty("User-Agent", "KernelSU-Next/${BuildConfig.VERSION_CODE}")

            val pageHtml = BufferedReader(InputStreamReader(pageConnection.getInputStream())).use {
                it.readText()
            }

            val zipUrlMatch = """href="(/[^"]+/releases/download/[^"]+\.zip)"""".toRegex()
                .find(pageHtml)?.groupValues?.get(1)

            ReleaseInfoModuleRepo(
                version = tagMatch,
                zipUrl = if (zipUrlMatch != null) "https://github.com$zipUrlMatch" else "",
                stars = stars
            )
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
    }
}

data class ReleaseInfoModuleRepo(
    val version: String,
    val zipUrl: String,
    val stars: Int = -1
)

@Preview
@Composable
private fun ModuleRepoScreenPreview() {
    ModuleRepoScreen(EmptyDestinationsNavigator)
}