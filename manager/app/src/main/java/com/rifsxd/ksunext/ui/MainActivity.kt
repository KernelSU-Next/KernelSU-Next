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
import com.ramcosta.composedestinations.utils.rememberDestinationsNavigator
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ui.screen.FlashIt
import com.rifsxd.ksunext.ui.theme.KernelSUTheme
import com.rifsxd.ksunext.ui.util.*
import com.rifsxd.ksunext.ui.viewmodel.ModuleViewModel
import com.rifsxd.ksunext.ui.viewmodel.SuperUserViewModel

class MainActivity : ComponentActivity() {

    var zipUri: ArrayList<Uri>? = null
    var navigateLoc by mutableStateOf("")
    private val handler = Handler(Looper.getMainLooper())

    override fun attachBaseContext(newBase: Context?) {
        super.attachBaseContext(newBase?.let { LocaleHelper.applyLanguage(it) })
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            window.isNavigationBarContrastEnforced = false
        }

        super.onCreate(savedInstanceState)

        val isManager = Natives.becomeManager(packageName)
        if (isManager) install()

        if ((intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0) {
            intent.extras?.clear()
            intent = null
        }

        if(intent != null)
            handleIntent(intent)

        setContent {
            val prefs = getSharedPreferences("settings", MODE_PRIVATE)
            val amoledMode = prefs.getBoolean("enable_amoled", false)

            val moduleViewModel: ModuleViewModel = viewModel()
            val superUserViewModel: SuperUserViewModel = viewModel()

            KernelSUTheme(amoledMode = amoledMode) {
                val navController = rememberNavController()
                val snackBarHostState = remember { SnackbarHostState() }
                val navigator = navController.rememberDestinationsNavigator()

                LaunchedEffect(zipUri, navigateLoc) {
                    if (!zipUri.isNullOrEmpty()) {
                        navigator.navigate(
                            FlashScreenDestination(
                                flashIt = FlashIt.FlashModules(zipUri!!),
                                finishIntent = true
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
                        }
                        navigateLoc = ""
                    }
                }

                LaunchedEffect(Unit) {
                    if (superUserViewModel.appList.isEmpty()) {
                        superUserViewModel.fetchAppList()
                    }
                    if (moduleViewModel.moduleList.isEmpty()) {
                        moduleViewModel.fetchModuleList()
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
                                .windowInsetsPadding(WindowInsets.navigationBars),
                            navGraph = NavGraphs.root,
                            navController = navController,
                            defaultTransitions = object : NavHostAnimatedDestinationStyle() {
                                override val enterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
                                    slideInHorizontally(
                                        initialOffsetX = { it },
                                        animationSpec = spring(stiffness = Spring.StiffnessMediumLow)
                                    ) + fadeIn(animationSpec = tween(400))
                                }

                                override val exitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
                                    slideOutHorizontally(
                                        targetOffsetX = { -it / 2 },
                                        animationSpec = spring(stiffness = Spring.StiffnessMediumLow)
                                    ) + fadeOut(animationSpec = tween(400))
                                }

                                override val popEnterTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> EnterTransition = {
                                    fadeIn(animationSpec = tween(340))
                                }

                                override val popExitTransition: AnimatedContentTransitionScope<NavBackStackEntry>.() -> ExitTransition = {
                                    scaleOut(targetScale = 0.85f, animationSpec = spring(stiffness = Spring.StiffnessLow)) +
                                    fadeOut(animationSpec = tween(400))
                                }
                            }
                        )
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        // Handle cases where the app is already running when a shortcut is tapped
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent) {
        when (intent.action) {
            "com.rifsxd.ksunext.ACTION_REBOOT" -> {
                Toast.makeText(this, "Rebooting in 5 secs!", Toast.LENGTH_SHORT).show()

                handler.postDelayed({
                    reboot("")
                }, 5000)
            }
            "com.rifsxd.ksunext.ACTION_SUPERUSER" -> {
                navigateLoc = "superuser"
            }
            "com.rifsxd.ksunext.ACTION_MODULES" -> {
                navigateLoc = "modules"
            }
            "android.intent.action.VIEW" -> {
                zipUri = if (intent.data != null) {
                    arrayListOf(intent.data!!)
                } else {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableArrayListExtra("uris", Uri::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableArrayListExtra("uris")
                    }
                }
            }
        }
        setIntent(null)
    }
}
