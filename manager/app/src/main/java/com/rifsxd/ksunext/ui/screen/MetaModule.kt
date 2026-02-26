package com.rifsxd.ksunext.ui.screen

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.CloudDownload
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material3.*
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
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.dropUnlessResumed
import com.ramcosta.composedestinations.annotation.Destination
import com.ramcosta.composedestinations.annotation.RootGraph
import com.ramcosta.composedestinations.generated.destinations.FlashScreenDestination
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
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import androidx.lifecycle.viewmodel.compose.viewModel
import com.dergoogler.mmrl.ui.component.LabelItem
import com.dergoogler.mmrl.ui.component.LabelItemDefaults
import com.rifsxd.ksunext.ui.viewmodel.ModuleViewModel
import org.json.JSONArray
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.URL
import android.content.Intent

data class MetaModule(
    val id: String,
    val name: String,
    val description: String,
    val author: String,
    val repoUrl: String,
    val bannerLink: String = "",
    val license: String = "",
    val visibility: Int = 1,
    val latestVersion: String = "",
    val downloadUrl: String = "",
    val isLoading: Boolean = true
)

sealed class MetaModuleState {
    object Loading : MetaModuleState()
    data class Success(val modules: List<MetaModule>) : MetaModuleState()
    data class Error(val message: String) : MetaModuleState()
}

/**
 * @author rifsxd
 * @date 2026/1/29.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Destination<RootGraph>
@Composable
fun MetaModuleScreen(navigator: DestinationsNavigator) {
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())
    // Bottom bar scroll tracking
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
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + 112.dp
    val modulesJsonUrl = "https://raw.githubusercontent.com/KernelSU-Next/KernelSU-Next-Modules-Repo/refs/heads/main/meta_modules.json"

    var moduleState by remember { mutableStateOf<MetaModuleState>(MetaModuleState.Loading) }
    var selectedModule by remember { mutableStateOf<MetaModule?>(null) }
    var downloadingModuleId by remember { mutableStateOf<String?>(null) }
    var downloadedUri by remember { mutableStateOf<Uri?>(null) }

    DownloadListener(context) { uri ->
        downloadedUri = uri
        downloadingModuleId = null
        navigator.navigate(
            FlashScreenDestination(FlashIt.FlashModules(listOf(uri)))
        )
    }

    val moduleViewModel = viewModel<ModuleViewModel>()

    suspend fun fetchMetaModulesFromJson(jsonUrl: String): List<MetaModule>? {
        return withContext(Dispatchers.IO) {
            try {
                val conn = URL(jsonUrl).openConnection() as java.net.HttpURLConnection
                conn.setRequestProperty("User-Agent", "KernelSU/${BuildConfig.VERSION_CODE}")
                val text = BufferedReader(InputStreamReader(conn.inputStream)).use { it.readText() }
                conn.disconnect()

                val arr = JSONArray(text)
                val out = mutableListOf<MetaModule>()
                for (i in 0 until arr.length()) {
                    val obj = arr.getJSONObject(i)
                    out += MetaModule(
                        id = obj.optString("id"),
                        name = obj.optString("name"),
                        description = obj.optString("description"),
                        author = obj.optString("author"),
                        repoUrl = obj.optString("repoUrl"),
                        bannerLink = obj.optString("bannerUrl"),
                        license = obj.optString("license", ""),
                        visibility = obj.optInt("visibility", 1)
                    )
                }
                out
            } catch (e: Exception) {
                e.printStackTrace()
                null
            }
        }
    }

    suspend fun loadModules() {
        try {
            moduleState = MetaModuleState.Loading

            val baseList = withContext(Dispatchers.IO) { fetchMetaModulesFromJson(modulesJsonUrl) }

            if (baseList == null) {
                moduleState = MetaModuleState.Error("Failed to load module list")
                return
            }

            // Filter out modules with visibility = 0
            val visibleModules = baseList.filter { it.visibility == 1 }

            moduleState = MetaModuleState.Success(
                visibleModules.map { it.copy(latestVersion = "", downloadUrl = "", isLoading = true) }
            )

            kotlinx.coroutines.coroutineScope {
                visibleModules.forEach { baseModule ->
                    launch {
                        try {
                            val releaseInfo = withContext(Dispatchers.IO) { fetchLatestReleaseInfo(baseModule.repoUrl) }
                            val updated = baseModule.copy(
                                latestVersion = releaseInfo?.version ?: "null",
                                downloadUrl = releaseInfo?.zipUrl ?: "",
                                isLoading = false
                            )

                            val current = moduleState
                            if (current is MetaModuleState.Success) {
                                val mutable = current.modules.toMutableList()
                                val idx = mutable.indexOfFirst { it.id == baseModule.id }
                                if (idx >= 0) mutable[idx] = updated else mutable.add(updated)
                                moduleState = MetaModuleState.Success(mutable)
                            }
                        } catch (e: Exception) {
                            val updated = baseModule.copy(latestVersion = "null", downloadUrl = "", isLoading = false)
                            val current = moduleState
                            if (current is MetaModuleState.Success) {
                                val mutable = current.modules.toMutableList()
                                val idx = mutable.indexOfFirst { it.id == baseModule.id }
                                if (idx >= 0) mutable[idx] = updated else mutable.add(updated)
                                moduleState = MetaModuleState.Success(mutable)
                            }
                        }
                    }
                }
            }
        } catch (e: Exception) {
            moduleState = MetaModuleState.Error(e.message ?: "Unknown error")
        }
    }

    LaunchedEffect(Unit) {
        scope.launch {
            loadModules()
        }
    }

    Scaffold(
        topBar = {
            TopBar(
                onBack = dropUnlessResumed { navigator.popBackStack() },
                onRefresh = {
                    scope.launch {
                        moduleState = MetaModuleState.Loading
                        loadModules()
                    }
                },
                scrollBehavior = scrollBehavior
            )
        },
        snackbarHost = { SnackbarHost(snackBarHost, modifier = Modifier.padding(bottom = navBarPadding)) },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal)
    ) { paddingValues ->
        when (val state = moduleState) {
            is MetaModuleState.Loading -> {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator()
                }
            }
            is MetaModuleState.Success -> {
                val installedIds = moduleViewModel.moduleList.map { it.id }
                
                val hasInstalledMetaModule = state.modules.any { installedIds.contains(it.id) }
                
                // Sort modules alphabetically by name (A to Z)
                val sortedModules = state.modules.sortedBy { it.name.lowercase() }
                
                LazyColumn(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues)
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
                        val isInstalled = installedIds.contains(module.id)
                        val isThisModuleDownloading = downloadingModuleId == module.id
                        
                        val isInstallEnabled = when {
                            downloadingModuleId != null && !isThisModuleDownloading -> false
                            isInstalled -> true
                            hasInstalledMetaModule -> false
                            else -> true
                        }

                        MetaModuleCard(
                            module = module,
                            isInstalled = isInstalled,
                            isInstallEnabled = isInstallEnabled,
                            isDownloading = isThisModuleDownloading,
                            onCardClick = {
                                val intent = Intent(Intent.ACTION_VIEW, Uri.parse(module.repoUrl))
                                context.startActivity(intent)
                            },
                            onDownload = { selectedModule ->
                                downloadingModuleId = selectedModule.id
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
                                        downloadingModuleId = null
                                    }
                                }
                            }
                        )
                    }
                }
            }
            is MetaModuleState.Error -> {
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
                                    moduleState = MetaModuleState.Loading
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
private fun MetaModuleCard(
    module: MetaModule,
    isInstalled: Boolean = false,
    isInstallEnabled: Boolean = true,
    isDownloading: Boolean = false,
    onCardClick: () -> Unit,
    onDownload: (MetaModule) -> Unit,
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
                Row(modifier = Modifier.fillMaxWidth()) {
                    if (module.license.isNotEmpty()) {
                        LabelItem(
                            text = module.license,
                            style = LabelItemDefaults.style.copy(
                                containerColor = MaterialTheme.colorScheme.primaryContainer,
                                contentColor = MaterialTheme.colorScheme.onPrimaryContainer
                            )
                        )
                        Spacer(modifier = Modifier.width(8.dp))
                    }

                    if (isInstalled) {
                        LabelItem(
                            text = stringResource(R.string.installed),
                            style = LabelItemDefaults.style.copy(
                                containerColor = MaterialTheme.colorScheme.secondaryContainer,
                                contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                            )
                        )
                    }
                }

                if (module.license.isNotEmpty() || isInstalled) {
                    Spacer(modifier = Modifier.height(8.dp))
                }
                
                Row(
                    modifier = Modifier
                        .fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(
                        modifier = Modifier.weight(1f)
                    ) {
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
                            text = if (isInstalled) stringResource(R.string.reinstall) else stringResource(R.string.install),
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
    onRefresh: () -> Unit = {},
    scrollBehavior: TopAppBarScrollBehavior? = null
) {
    TopAppBar(
        title = {
            Text(
                text = stringResource(R.string.meta_module_screen),
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
            IconButton(onClick = onRefresh) {
                Icon(Icons.Default.Sync, contentDescription = "Refresh")
            }
        },
        windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        scrollBehavior = scrollBehavior
    )
}

private suspend fun fetchLatestReleaseInfo(repoUrl: String): ReleaseInfo? {
    return withContext(Dispatchers.IO) {
        try {
            val latestUrl = "$repoUrl/releases/latest"
            val connection = URL(latestUrl).openConnection() as java.net.HttpURLConnection
            connection.instanceFollowRedirects = false
            connection.setRequestProperty("User-Agent", "KernelSU/${BuildConfig.VERSION_CODE}")
            
            val redirectUrl = connection.getHeaderField("Location")
            connection.disconnect()
            
            if (redirectUrl == null) return@withContext null
            
            val tagMatch = """/tag/([^/?\s]+)""".toRegex()
                .find(redirectUrl)?.groupValues?.get(1)
            
            if (tagMatch == null) return@withContext null
            
            val releasePageUrl = "$repoUrl/releases/expanded_assets/$tagMatch"
            val pageConnection = URL(releasePageUrl).openConnection()
            pageConnection.setRequestProperty("User-Agent", "KernelSU/${BuildConfig.VERSION_CODE}")
            
            val pageHtml = BufferedReader(InputStreamReader(pageConnection.getInputStream())).use {
                it.readText()
            }
        
            val zipUrlMatch = """href="(/[^"]+/releases/download/[^"]+\.zip)"""".toRegex()
                .find(pageHtml)?.groupValues?.get(1)
            
            if (zipUrlMatch != null) {
                return@withContext ReleaseInfo(
                    version = tagMatch,
                    zipUrl = "https://github.com$zipUrlMatch"
                )
            }
            
            null
        } catch (e: Exception) {
            e.printStackTrace()
            null
        }
    }
}

data class ReleaseInfo(
    val version: String,
    val zipUrl: String
)

@Preview
@Composable
private fun MetaModuleScreenPreview() {
    MetaModuleScreen(EmptyDestinationsNavigator)
}