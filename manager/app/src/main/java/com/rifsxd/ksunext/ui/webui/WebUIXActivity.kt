package com.rifsxd.ksunext.ui.webui

import android.app.ActivityManager
import android.os.Build
import android.os.Bundle
import android.webkit.WebView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.lifecycle.lifecycleScope
import com.dergoogler.mmrl.platform.Platform
import com.dergoogler.mmrl.webui.component.Loading
import com.dergoogler.mmrl.webui.model.JavaScriptInterface
import com.dergoogler.mmrl.webui.screen.WebUIScreen
import com.dergoogler.mmrl.webui.util.rememberWebUIOptions
import com.rifsxd.ksunext.BuildConfig
import com.rifsxd.ksunext.ui.theme.KernelSUTheme
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class WebUIXActivity : ComponentActivity() {
    private lateinit var webView: WebView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        webView = WebView(this)

        lifecycleScope.launch {
            KsuLibSuProvider.initPlatform()
        }

        val moduleId = intent.getStringExtra("id")!!
        val name = intent.getStringExtra("name")!!
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            @Suppress("DEPRECATION")
            setTaskDescription(ActivityManager.TaskDescription("KernelSU - $name"))
        } else {
            val taskDescription =
                ActivityManager.TaskDescription.Builder().setLabel("KernelSU - $name").build()
            setTaskDescription(taskDescription)
        }

        val prefs = getSharedPreferences("settings", MODE_PRIVATE)

        setContent {
            KernelSUTheme {
                var isLoading by remember { mutableStateOf(true) }

                LaunchedEffect(Platform.isAlive) {
                    while (!Platform.isAlive) {
                        delay(1000)
                    }

                    isLoading = false
                }

                if (isLoading) {
                    Loading()

                    return@KernelSUTheme
                }

                val webDebugging = prefs.getBoolean("enable_web_debugging", false)
                val dark = isSystemInDarkTheme()

                val options = rememberWebUIOptions(
                    modId = moduleId,
                    debug = webDebugging,
                    appVersionCode = BuildConfig.VERSION_CODE,
                    isDarkMode = dark,
                )

                WebUIScreen(
                    webView = webView,
                    options = options,
                    interfaces = listOf(
                        JavaScriptInterface(
                            name = "ksu",
                            instance = WebViewInterface(
                                context = this@WebUIXActivity,
                                webView = webView,
                                modDir = "/data/adb/modules/$moduleId"
                            ),
                        )
                    )
                )
            }
        }
    }
}