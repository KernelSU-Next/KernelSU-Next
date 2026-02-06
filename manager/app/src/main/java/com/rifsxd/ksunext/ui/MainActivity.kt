package com.rifsxd.ksunext.ui

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.*
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavBackStackEntry
import androidx.navigation.NavHostController
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.ramcosta.composedestinations.DestinationsNavHost
import com.ramcosta.composedestinations.animations.NavHostAnimatedDestinationStyle
import com.ramcosta.composedestinations.generated.NavGraphs
import com.ramcosta.composedestinations.generated.destinations.ExecuteModuleActionScreenDestination
import com.ramcosta.composedestinations.generated.destinations.FlashScreenDestination
import com.ramcosta.composedestinations.generated.destinations.ModuleScreenDestination
import com.ramcosta.composedestinations.generated.destinations.SuperUserScreenDestination
import com.ramcosta.composedestinations.generated.destinations.SettingScreenDestination
import com.ramcosta.composedestinations.utils.isRouteOnBackStackAsState
import com.ramcosta.composedestinations.utils.rememberDestinationsNavigator
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.ui.screen.BottomBarDestination
import com.rifsxd.ksunext.ui.screen.FlashIt
import com.rifsxd.ksunext.ui.theme.KernelSUTheme
import com.rifsxd.ksunext.ui.util.*
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.unit.Velocity
import kotlin.math.abs

data class ScrollState(
    val isScrollingDown: MutableState<Boolean>,
    val scrollOffset: MutableState<Float>,
    val previousScrollOffset: MutableState<Float>
)

val LocalScrollState = compositionLocalOf<ScrollState?> { null }

@Composable
fun rememberScrollConnection(
    isScrollingDown: MutableState<Boolean>,
    scrollOffset: MutableState<Float>,
    previousScrollOffset: MutableState<Float>,
    threshold: Float = 50f
): NestedScrollConnection {
    return remember {
        object : NestedScrollConnection {
            override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
                val delta = available.y
                
                // Update scroll offset
                val newOffset = scrollOffset.value + delta
                scrollOffset.value = newOffset
                
                // Calculate the scroll delta from previous offset
                val scrollDelta = previousScrollOffset.value - newOffset
                
                // Only update direction if scroll delta exceeds threshold
                if (abs(scrollDelta) > threshold) {
                    isScrollingDown.value = scrollDelta > 0
                    previousScrollOffset.value = newOffset
                }
                
                return Offset.Zero
            }
            
            override suspend fun onPostFling(consumed: Velocity, available: Velocity): Velocity {
                // Reset offset tracking after fling
                previousScrollOffset.value = scrollOffset.value
                return super.onPostFling(consumed, available)
            }
        }
    }
}

fun Modifier.trackScroll(
    isScrollingDown: MutableState<Boolean>,
    scrollOffset: MutableState<Float>,
    previousScrollOffset: MutableState<Float>,
    threshold: Float = 50f
): Modifier {
    val scrollConnection = object : NestedScrollConnection {
        override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
            val delta = available.y
            
            // Update scroll offset
            val newOffset = scrollOffset.value + delta
            scrollOffset.value = newOffset
            
            // Calculate the scroll delta from previous offset
            val scrollDelta = previousScrollOffset.value - newOffset
            
            // Only update direction if scroll delta exceeds threshold
            if (abs(scrollDelta) > threshold) {
                isScrollingDown.value = scrollDelta > 0
                previousScrollOffset.value = newOffset
            }
            
            return Offset.Zero
        }
        
        override suspend fun onPostFling(consumed: Velocity, available: Velocity): Velocity {
            // Reset offset tracking after fling
            previousScrollOffset.value = scrollOffset.value
            return super.onPostFling(consumed, available)
        }
    }
    
    return this.nestedScroll(scrollConnection)
}

class MainActivity : ComponentActivity() {

    var zipUri by mutableStateOf<ArrayList<Uri>?>(null)
    var navigateLoc by mutableStateOf("")
    var moduleActionId by mutableStateOf<String?>(null)
    var amoledModeState = mutableStateOf(false)
    private val handler = Handler(Looper.getMainLooper())

    override fun attachBaseContext(newBase: Context?) {
        super.attachBaseContext(newBase?.let { LocaleHelper.applyLanguage(it) })
    }

    override fun onCreate(savedInstanceState: Bundle?) {

        super.onCreate(savedInstanceState)

        enableEdgeToEdge()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            window.isNavigationBarContrastEnforced = false
        }

        try {
            val prefsInit = getSharedPreferences("settings", MODE_PRIVATE)
            amoledModeState.value = prefsInit.getBoolean("enable_amoled", false)
        } catch (_: Exception) {}

        val isManager = Natives.isManager
        if (isManager) install()

        if ((intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0) {
            intent.extras?.clear()
            intent = null
        }

        if(intent != null)
            handleIntent(intent)

        setContent {
            KernelSUTheme(amoledMode = amoledModeState.value) {
                val navController = rememberNavController()
                val snackBarHostState = remember { SnackbarHostState() }
                val currentDestination = navController.currentBackStackEntryAsState().value?.destination
                val bottomBarRoutes = remember {
                    BottomBarDestination.entries.map { it.direction.route }.toSet()
                }
                val navigator = navController.rememberDestinationsNavigator()

                val currentBackStackEntry by navController.currentBackStackEntryAsState()
                val currentRoute = currentBackStackEntry?.destination?.route

                val homeDestination = BottomBarDestination.entries.firstOrNull()
                val startRoute = homeDestination?.direction?.route

                    if (homeDestination != null && startRoute != null) {
                    BackHandler(enabled = currentRoute != startRoute && currentRoute in bottomBarRoutes) {
                        navigator.navigate(homeDestination.direction) {
                            popUpTo(NavGraphs.root) {
                                saveState = true
                            }
                            launchSingleTop = true
                            restoreState = true
                        }
                    }
                }

                // Track the last bottom bar destination index for directional animations
                var lastBottomBarIndex by remember { mutableStateOf(0) }
                var isBottomBarNavigation by remember { mutableStateOf(false) }
                
                // Scroll state for bottom bar visibility
                val isScrollingDown = remember { mutableStateOf(false) }
                val scrollOffset = remember { mutableStateOf(0f) }
                val previousScrollOffset = remember { mutableStateOf(0f) }

                LaunchedEffect(zipUri, navigateLoc, moduleActionId) {
                    if (moduleActionId != null) {
                        navigator.navigate(ExecuteModuleActionScreenDestination(moduleActionId!!))
                        moduleActionId = null
                    }

                    if (!zipUri.isNullOrEmpty()) {
                        val uris = zipUri!!
                        val component = intent?.component?.className
                        val flashIt = when {
                            component?.endsWith("FlashAnyKernel") == true -> FlashIt.FlashAnyKernel(uris.first())
                            else -> FlashIt.FlashModules(uris)
                        }
                        
                        navigator.navigate(
                            FlashScreenDestination(flashIt = flashIt)
                        )
                        zipUri = null
                    }

                    if(zipUri.isNullOrEmpty() && navigateLoc != "")
                    {
                        when(navigateLoc) {
                            "superuser" -> {
                                navigator.navigate(SuperUserScreenDestination)
                            }
                            "modules" -> {
                                navigator.navigate(ModuleScreenDestination)
                            }
                            "settings" -> {
                                navigator.navigate(SettingScreenDestination)
                            }
                        }
                        navigateLoc = ""
                    }
                }

                val showBottomBar = when (currentDestination?.route) {
                    FlashScreenDestination.route -> false // Hide for FlashScreenDestination
                    ExecuteModuleActionScreenDestination.route -> false // Hide for ExecuteModuleActionScreen
                    else -> !isScrollingDown.value
                }

                Scaffold(
                    contentWindowInsets = WindowInsets(0, 0, 0, 0)
                ) { innerPadding ->
                    Box(modifier = Modifier.fillMaxSize()) {
                        CompositionLocalProvider(
                            LocalSnackbarHost provides snackBarHostState,
                            LocalScrollState provides ScrollState(
                                isScrollingDown = isScrollingDown,
                                scrollOffset = scrollOffset,
                                previousScrollOffset = previousScrollOffset
                            )
                        ) {
                            DestinationsNavHost(
                                modifier = Modifier
                                    .padding(innerPadding)
                                    .fillMaxSize(),
                                navGraph = NavGraphs.root,
                                navController = navController,
                                defaultTransitions = object : NavHostAnimatedDestinationStyle() {
                                    override val enterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
                                        val targetRoute = targetState.destination.route
                                        val initialRoute = initialState.destination.route
                                        
                                        // Check if both are bottom bar destinations
                                        val targetIndex = BottomBarDestination.entries.indexOfFirst { it.direction.route == targetRoute }
                                        val initialIndex = BottomBarDestination.entries.indexOfFirst { it.direction.route == initialRoute }
                                        
                                        if (targetIndex != -1 && initialIndex != -1) {
                                            // Bottom bar navigation - use horizontal slide based on direction
                                            val slideDirection = if (targetIndex > initialIndex) 1 else -1
                                            slideInHorizontally(
                                                initialOffsetX = { it * slideDirection },
                                                animationSpec = tween(
                                                    durationMillis = 300,
                                                    easing = FastOutSlowInEasing
                                                )
                                            ) + fadeIn(
                                                animationSpec = tween(300, easing = LinearOutSlowInEasing)
                                            )
                                        } else {
                                            // Default navigation - slide from right
                                            slideInHorizontally(
                                                initialOffsetX = { it },
                                                animationSpec = tween(
                                                    durationMillis = 300,
                                                    easing = FastOutSlowInEasing
                                                )
                                            ) + fadeIn(
                                                animationSpec = tween(300, easing = LinearOutSlowInEasing)
                                            )
                                        }
                                    }

                                    override val exitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
                                        val targetRoute = targetState.destination.route
                                        val initialRoute = initialState.destination.route
                                        
                                        val targetIndex = BottomBarDestination.entries.indexOfFirst { it.direction.route == targetRoute }
                                        val initialIndex = BottomBarDestination.entries.indexOfFirst { it.direction.route == initialRoute }
                                        
                                        if (targetIndex != -1 && initialIndex != -1) {
                                            // Bottom bar navigation - slide out in opposite direction
                                            val slideDirection = if (targetIndex > initialIndex) -1 else 1
                                            slideOutHorizontally(
                                                targetOffsetX = { it * slideDirection / 3 },
                                                animationSpec = tween(
                                                    durationMillis = 300,
                                                    easing = FastOutSlowInEasing
                                                )
                                            ) + fadeOut(
                                                animationSpec = tween(250, easing = LinearOutSlowInEasing)
                                            )
                                        } else {
                                            // Default navigation
                                            slideOutHorizontally(
                                                targetOffsetX = { -it / 3 },
                                                animationSpec = tween(
                                                    durationMillis = 300,
                                                    easing = FastOutSlowInEasing
                                                )
                                            ) + fadeOut(
                                                animationSpec = tween(250, easing = LinearOutSlowInEasing)
                                            )
                                        }
                                    }

                                    override val popEnterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
                                        slideInHorizontally(
                                            initialOffsetX = { -it / 3 },
                                            animationSpec = tween(
                                                durationMillis = 280,
                                                easing = FastOutSlowInEasing
                                            )
                                        ) + fadeIn(
                                            animationSpec = tween(280, easing = LinearOutSlowInEasing)
                                        )
                                    }

                                    override val popExitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
                                        slideOutHorizontally(
                                            targetOffsetX = { it / 3 },
                                            animationSpec = tween(
                                                durationMillis = 280,
                                                easing = FastOutSlowInEasing
                                            )
                                        ) + fadeOut(
                                            animationSpec = tween(250, easing = LinearOutSlowInEasing)
                                        )
                                    }
                                }
                            )
                        }
                        
                        // Floating Bottom Bar as overlay
                        AnimatedVisibility(
                            visible = showBottomBar,
                            modifier = Modifier.align(Alignment.BottomCenter),
                            enter = slideInVertically(initialOffsetY = { it }) + fadeIn(),
                            exit = slideOutVertically(targetOffsetY = { it }) + fadeOut()
                        ) {
                            BottomBar(navController)
                        }
                    }
                }
            }
        }
    }

    fun setAmoledMode(enabled: Boolean) {
        try {
            val prefs = getSharedPreferences("settings", MODE_PRIVATE)
            prefs.edit().putBoolean("enable_amoled", enabled).apply()
        } catch (_: Exception) {}
        amoledModeState.value = enabled
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
        setIntent(intent)
    }

    private fun handleIntent(intent: Intent) {
        val shortcutType = intent.getStringExtra("shortcut_type")
        if (shortcutType == "module_action") {
            moduleActionId = intent.getStringExtra("module_id")
        }

        when (intent.action) {
            Intent.ACTION_VIEW -> {
                zipUri =
                    intent.data?.let { arrayListOf(it) }
                        ?: if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            intent.getParcelableArrayListExtra("uris", Uri::class.java)
                        } else {
                            @Suppress("DEPRECATION")
                            intent.getParcelableArrayListExtra("uris")
                        }
            }

            "ACTION_SETTINGS" -> navigateLoc = "settings"
            "ACTION_SUPERUSER" -> navigateLoc = "superuser"
            "ACTION_MODULES" -> navigateLoc = "modules"
        }
    }
}

@Composable
private fun BottomBar(navController: NavHostController) {
    val navigator = navController.rememberDestinationsNavigator()
    val isManager = Natives.isManager
    val fullFeatured = isManager && !Natives.requireNewKernel() && rootAvailable()

    // Get current selected index with visible destinations
    val visibleDestinations = remember(fullFeatured) {
        BottomBarDestination.entries.filter { fullFeatured || !it.rootRequired }
    }
    val selectedIndex = visibleDestinations.indexOfFirst { 
        navController.isRouteOnBackStackAsState(it.direction).value 
    }.coerceAtLeast(0)
    
    // Animate the indicator position with jelly/spring effect
    val animatedSelectedIndex by animateFloatAsState(
        targetValue = selectedIndex.toFloat(),
        animationSpec = spring(
            dampingRatio = Spring.DampingRatioMediumBouncy,
            stiffness = Spring.StiffnessLow
        ),
        label = "selectedIndex"
    )

    // Responsive padding based on screen width
    BoxWithConstraints(
        modifier = Modifier
            .fillMaxWidth()
            .padding(
                bottom = WindowInsets.navigationBars
                    .asPaddingValues()
                    .calculateBottomPadding()
            )
    ) {
        val screenWidth = maxWidth
        val horizontalScreenPadding = when {
            screenWidth > 600.dp -> 32.dp // Tablet/Large screen
            screenWidth > 400.dp -> 24.dp // Normal phone
            else -> 16.dp // Small phone
        }
        
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = horizontalScreenPadding, vertical = 16.dp),
            contentAlignment = Alignment.Center
        ) {
            Surface(
                modifier = Modifier.wrapContentWidth(),
                shape = MaterialTheme.shapes.large,
                tonalElevation = 3.dp,
                shadowElevation = 8.dp
            ) {
                val itemSize = 64.dp
                val itemSpacing = 4.dp
                val containerPadding = 8.dp // Reduced to match vertical padding
                
                // Calculate exact width based on items
                val navBarWidth = (itemSize * visibleDestinations.size) + 
                                 (itemSpacing * (visibleDestinations.size - 1)) + 
                                 (containerPadding * 2)
                
                Box(
                    modifier = Modifier
                        .width(navBarWidth)
                        .height(80.dp)
                ) {
                    var totalWidth by remember { mutableStateOf(0) }
                    
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(horizontal = containerPadding)
                            .onSizeChanged { size ->
                                totalWidth = size.width
                            }
                    ) {
                        // Animated sliding indicator
                        if (totalWidth > 0 && visibleDestinations.isNotEmpty()) {
                            val density = LocalDensity.current
                            val itemSizePx = with(density) { itemSize.toPx() }
                            val itemSpacingPx = with(density) { itemSpacing.toPx() }
                            
                            // Calculate offset: each item position = (itemSize + spacing) * index
                            val indicatorOffset = (itemSizePx + itemSpacingPx) * animatedSelectedIndex
                            
                            Box(
                                modifier = Modifier
                                    .fillMaxHeight()
                                    .padding(vertical = 8.dp)
                                    .offset {
                                        androidx.compose.ui.unit.IntOffset(
                                            x = indicatorOffset.toInt(),
                                            y = 0
                                        )
                                    }
                                    .width(itemSize),
                                contentAlignment = Alignment.Center
                            ) {
                                Box(
                                    modifier = Modifier
                                        .size(itemSize)
                                        .background(
                                            color = MaterialTheme.colorScheme.secondaryContainer,
                                            shape = MaterialTheme.shapes.large
                                        )
                                )
                            }
                        }
                        
                        // Navigation items
                        Row(
                            modifier = Modifier.fillMaxSize(),
                            horizontalArrangement = Arrangement.spacedBy(itemSpacing),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            visibleDestinations.forEach { destination ->
                                val isCurrentDestOnBackStack by navController.isRouteOnBackStackAsState(destination.direction)
                                
                                Box(
                                    modifier = Modifier
                                        .size(itemSize)
                                        .clip(MaterialTheme.shapes.large)
                                        .clickable {
                                            if (isCurrentDestOnBackStack) {
                                                navigator.popBackStack(destination.direction, false)
                                            }
                                            navigator.navigate(destination.direction) {
                                                popUpTo(NavGraphs.root) {
                                                    saveState = true
                                                }
                                                launchSingleTop = true
                                                restoreState = true
                                            }
                                        },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Icon(
                                        if (isCurrentDestOnBackStack) destination.iconSelected else destination.iconNotSelected,
                                        stringResource(destination.label),
                                        tint = if (isCurrentDestOnBackStack) {
                                            MaterialTheme.colorScheme.onSecondaryContainer
                                        } else {
                                            MaterialTheme.colorScheme.onSurfaceVariant
                                        }
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
