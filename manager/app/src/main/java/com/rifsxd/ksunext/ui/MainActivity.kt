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
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.animation.*
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearOutSlowInEasing
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavBackStackEntry
import androidx.navigation.compose.rememberNavController
import com.ramcosta.composedestinations.DestinationsNavHost
import com.ramcosta.composedestinations.animations.NavHostAnimatedDestinationStyle
import com.ramcosta.composedestinations.generated.NavGraphs
import com.ramcosta.composedestinations.generated.destinations.FlashScreenDestination
import com.ramcosta.composedestinations.generated.destinations.ModuleScreenDestination
import com.ramcosta.composedestinations.generated.destinations.SuperUserScreenDestination
import com.ramcosta.composedestinations.generated.destinations.SettingScreenDestination
import com.ramcosta.composedestinations.utils.rememberDestinationsNavigator
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ui.screen.FlashIt
import com.rifsxd.ksunext.ui.theme.KernelSUTheme
import com.rifsxd.ksunext.ui.util.*

class MainActivity : ComponentActivity() {

    var zipUri by mutableStateOf<ArrayList<Uri>?>(null)
    var navigateLoc by mutableStateOf("")
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
                val navigator = navController.rememberDestinationsNavigator()

                LaunchedEffect(zipUri, navigateLoc) {
                    if (!zipUri.isNullOrEmpty()) {
                        navigator.navigate(
                            FlashScreenDestination(
                                flashIt = FlashIt.FlashModules(zipUri!!)
                            )
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

                Scaffold(
                    contentWindowInsets = WindowInsets(0, 0, 0, 0)
                ) { innerPadding ->
                    CompositionLocalProvider(
                        LocalSnackbarHost provides snackBarHostState,
                    ) {
                        DestinationsNavHost(
                            modifier = Modifier
                                .padding(innerPadding)
                                .fillMaxSize(),
                            navGraph = NavGraphs.root,
                            navController = navController,
                            defaultTransitions = object : NavHostAnimatedDestinationStyle() {
                                // smooth forward enter
                                override val enterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
                                    slideInHorizontally(
                                        initialOffsetX = { it }, // slide from right
                                        animationSpec = tween(
                                            durationMillis = 300,
                                            easing = FastOutSlowInEasing
                                        )
                                    ) + fadeIn(
                                        animationSpec = tween(300, easing = LinearOutSlowInEasing)
                                    )
                                }

                                // smooth forward exit
                                override val exitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
                                    slideOutHorizontally(
                                        targetOffsetX = { -it / 3 }, // subtle slide left
                                        animationSpec = tween(
                                            durationMillis = 300,
                                            easing = FastOutSlowInEasing
                                        )
                                    ) + fadeOut(
                                        animationSpec = tween(250, easing = LinearOutSlowInEasing)
                                    )
                                }

                                // pop back enter (backward navigation)
                                override val popEnterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
                                    slideInHorizontally(
                                        initialOffsetX = { -it / 3 }, // subtle from left
                                        animationSpec = tween(
                                            durationMillis = 280,
                                            easing = FastOutSlowInEasing
                                        )
                                    ) + fadeIn(
                                        animationSpec = tween(280, easing = LinearOutSlowInEasing)
                                    )
                                }

                                // pop back exit
                                override val popExitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
                                    slideOutHorizontally(
                                        targetOffsetX = { it / 3 }, // subtle slide right
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
