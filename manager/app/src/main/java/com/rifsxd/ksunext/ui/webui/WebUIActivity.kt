package com.rifsxd.ksunext.ui.webui

import android.annotation.SuppressLint
import android.app.Activity
import android.app.ActivityManager
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.util.Base64
import android.webkit.*
import android.widget.FrameLayout
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.enableEdgeToEdge
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.annotation.RequiresApi
import androidx.core.net.toUri
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.lifecycle.lifecycleScope
import androidx.webkit.WebViewAssetLoader
import com.rifsxd.ksunext.ui.util.createRootShell
import com.rifsxd.ksunext.ui.viewmodel.SuperUserViewModel
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import org.json.JSONObject
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

import androidx.fragment.app.FragmentActivity
import androidx.compose.ui.Modifier
import androidx.compose.foundation.layout.fillMaxSize

@SuppressLint("SetJavaScriptEnabled")
class WebUIActivity : FragmentActivity() {

    private lateinit var webviewInterface: WebViewInterface
    private val rootShell by lazy { createRootShell(true) }
    private var webView: WebView? = null
    private lateinit var container: FrameLayout
    private lateinit var insets: Insets
    private lateinit var moduleId: String

    private lateinit var fileChooserLauncher: ActivityResultLauncher<Intent>
    private var filePathCallback: ValueCallback<Array<Uri>>? = null

    private lateinit var saveFileLauncher: ActivityResultLauncher<Intent>
    private var pendingDownloadData: ByteArray? = null
    private var pendingDownloadSuggestedFilename: String? = null
    private var moduleName: String = ""

    private val superUserViewModel: SuperUserViewModel by viewModels()

    private var insetsContinuation: CancellableContinuation<Unit>? = null
    private var isInsetsEnabled = false


    private var appLockState = false
    private var isPromptShowing = false
    private lateinit var lockOverlay: android.view.View

    fun erudaConsole(context: android.content.Context): String {
        return context.assets.open("eruda.min.js").bufferedReader().use { it.readText() }
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            window.isNavigationBarContrastEnforced = false
        }

        super.onCreate(savedInstanceState)

        lifecycleScope.launch {
            if (SuperUserViewModel.apps.isEmpty()) {
                SuperUserViewModel().fetchAppList()
            }
        }

        fileChooserLauncher =
            registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
                if (result.resultCode == Activity.RESULT_OK) {
                    val data = result.data
                    var uris: Array<Uri>? = null

                    data?.dataString?.let {
                        uris = arrayOf(it.toUri())
                    }

                    data?.clipData?.let { clipData ->
                        uris = Array(clipData.itemCount) { i ->
                            clipData.getItemAt(i).uri
                        }
                    }

                    filePathCallback?.onReceiveValue(uris)
                } else {
                    filePathCallback?.onReceiveValue(null)
                }
                filePathCallback = null
            }

        saveFileLauncher =
            registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
                if (result.resultCode == Activity.RESULT_OK) {
                    result.data?.data?.let { uri ->
                        contentResolver.openOutputStream(uri)?.use { out ->
                            pendingDownloadData?.let { out.write(it) }
                        }
                    }
                }
                pendingDownloadData = null
                pendingDownloadSuggestedFilename = null
            }

        moduleId = intent.getStringExtra("id") ?: run {
            finishAndRemoveTask()
            return
        }

        val name = intent.getStringExtra("name") ?: run {
            finishAndRemoveTask()
            return
        }
        moduleName = name

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            @Suppress("DEPRECATION")
            setTaskDescription(ActivityManager.TaskDescription("WebUI-Next | $name"))
        } else {
            setTaskDescription(
                ActivityManager.TaskDescription.Builder()
                    .setLabel("WebUI-Next | $name")
                    .build()
            )
        }

        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        val developerOptionsEnabled = prefs.getBoolean("enable_developer_options", false)
        val enableWebDebugging = prefs.getBoolean("enable_web_debugging", false)

        if (savedInstanceState != null) {
            appLockState = savedInstanceState.getBoolean("appLockState", false)
            moduleName = savedInstanceState.getString("moduleName", "")
        } else {
            val isInternal = intent?.getBooleanExtra("is_internal_transition", false) ?: false
            if (isInternal) {
                com.rifsxd.ksunext.ui.util.AppLockManager.unlock()
            }
            appLockState = if (isInternal) false else prefs.getBoolean("enable_biometric_lock", false)
        }

        WebView.setWebContentsDebuggingEnabled(developerOptionsEnabled && enableWebDebugging)

        val moduleDir = "/data/adb/modules/$moduleId"
        val webRoot = File("$moduleDir/webroot")

        insets = Insets(0, 0, 0, 0)

        val assetLoader = WebViewAssetLoader.Builder()
            .setDomain("mui.kernelsu.org")
            .addPathHandler(
                "/",
                SuFilePathHandler(
                    this,
                    webRoot,
                    rootShell,
                    { insets },
                    { enable -> enableInsets(enable) }
                )
            )
            .build()

        container = FrameLayout(this)

        webView = WebView(this).apply {
            setBackgroundColor(Color.TRANSPARENT)

            val density = resources.displayMetrics.density

            ViewCompat.setOnApplyWindowInsetsListener(container) { view, windowInsets ->
                val inset = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())

                insets = Insets(
                    top = (inset.top / density).toInt(),
                    bottom = (inset.bottom / density).toInt(),
                    left = (inset.left / density).toInt(),
                    right = (inset.right / density).toInt()
                )

                if (isInsetsEnabled) {
                    view.setPadding(0, 0, 0, 0)
                } else {
                    view.setPadding(inset.left, inset.top, inset.right, inset.bottom)
                }

                insetsContinuation?.resumeWith(Result.success(Unit))
                insetsContinuation = null

                WindowInsetsCompat.CONSUMED
            }

            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.allowFileAccess = false

            addJavascriptInterface(
                WebViewInterface(this@WebUIActivity, this, moduleDir),
                "ksu"
            )

            webChromeClient = object : WebChromeClient() {

                override fun onShowFileChooser(
                    webView: WebView?,
                    filePathCallback: ValueCallback<Array<Uri>>?,
                    fileChooserParams: FileChooserParams?
                ): Boolean {
                    this@WebUIActivity.filePathCallback = filePathCallback

                    val intent =
                        fileChooserParams?.createIntent()
                            ?: Intent(Intent.ACTION_GET_CONTENT).apply {
                                type = "*/*"
                            }

                    if (fileChooserParams?.mode == FileChooserParams.MODE_OPEN_MULTIPLE) {
                        intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
                    }

                    return try {
                        fileChooserLauncher.launch(intent)
                        true
                    } catch (e: Exception) {
                        this@WebUIActivity.filePathCallback?.onReceiveValue(null)
                        this@WebUIActivity.filePathCallback = null
                        false
                    }
                }

                @RequiresApi(Build.VERSION_CODES.Q)
                override fun onConsoleMessage(consoleMessage: ConsoleMessage?): Boolean {
                    consoleMessage?.message()?.let { msg ->
                        val prefix = "ksuBlobData:"
                        if (msg.startsWith(prefix)) {
                            try {
                                val json = JSONObject(msg.substring(prefix.length))
                                saveDataUrlToDownloads(
                                    json.getString("dataUrl"),
                                    json.getString("mimeType")
                                )
                                return true
                            } catch (_: Exception) {
                            }
                        }
                    }
                    return super.onConsoleMessage(consoleMessage)
                }
            }

            webViewClient = object : WebViewClient() {

                override fun shouldInterceptRequest(
                    view: WebView,
                    request: WebResourceRequest
                ): WebResourceResponse? {
                    val url = request.url

                    // Handle ksu://icon/<packageName>
                    if (url.scheme.equals("ksu", true)
                        && url.host.equals("icon", true)
                    ) {
                        val packageName = url.path?.removePrefix("/")
                        if (!packageName.isNullOrEmpty()) {
                            val icon = AppIconUtil.loadAppIconSync(
                                this@WebUIActivity,
                                packageName,
                                512
                            )

                            if (icon != null) {
                                val out = ByteArrayOutputStream()
                                icon.compress(Bitmap.CompressFormat.PNG, 100, out)
                                return WebResourceResponse(
                                    "image/png",
                                    null,
                                    200,
                                    "OK",
                                    mapOf("Access-Control-Allow-Origin" to "*"),
                                    ByteArrayInputStream(out.toByteArray())
                                )
                            }
                        }
                    }

                    // Fallback to asset loader
                    return assetLoader.shouldInterceptRequest(request.url)
                }

                override fun shouldOverrideUrlLoading(view: WebView?, request: WebResourceRequest?): Boolean {
                    return false
                }

                override fun onPageFinished(view: WebView?, url: String?) {
                    if (developerOptionsEnabled && enableWebDebugging) {
                        view?.evaluateJavascript(erudaConsole(this@WebUIActivity), null)
                        view?.evaluateJavascript("eruda.init();", null)
                    }
                }
            }

            loadUrl("https://mui.kernelsu.org/index.html")
        }

        setupWebUIBackHandler()
        
        container.addView(webView)

        val prefsInit = getSharedPreferences("settings", MODE_PRIVATE)
        val requireBiometric = prefsInit.getBoolean("enable_biometric_lock", false)

        if (requireBiometric || appLockState) {
            val amoledMode = prefsInit.getBoolean("enable_amoled", false)
            val isDarkTheme = (resources.configuration.uiMode and android.content.res.Configuration.UI_MODE_NIGHT_MASK) == android.content.res.Configuration.UI_MODE_NIGHT_YES

            lockOverlay = android.view.View(this).apply {
                setBackgroundColor(
                    if (amoledMode && isDarkTheme) {
                        android.graphics.Color.BLACK
                    } else {
                        val typedValue = android.util.TypedValue()
                        theme.resolveAttribute(android.R.attr.colorBackground, typedValue, true)
                        typedValue.data
                    }
                )
                visibility = if (appLockState) android.view.View.VISIBLE else android.view.View.GONE
                isClickable = true
                isFocusable = true
            }
            container.addView(lockOverlay)
        }

        setContentView(container)

        // coroutine-safe inset wait
        if (insets == Insets(0, 0, 0, 0)) {
            lifecycleScope.launch {
                suspendCancellableCoroutine<Unit> { cont ->
                    insetsContinuation = cont
                    cont.invokeOnCancellation {
                        if (insetsContinuation === cont) {
                            insetsContinuation = null
                        }
                    }
                }
            }
        }
    }
    private fun setupWebUIBackHandler() {
        val backCallback = object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                val currentWebView = webView
                if (currentWebView != null && currentWebView.canGoBack()) {
                    currentWebView.goBack()
                } else {
                    isEnabled = false
                    finish()
                }
            }
        }

        onBackPressedDispatcher.addCallback(this, backCallback)
    }

    private fun extractMimeTypeAndBase64Data(dataUrl: String): Pair<String, String>? {
        if (!dataUrl.startsWith("data:")) return null
        val commaIndex = dataUrl.indexOf(',')
        if (commaIndex == -1) return null

        val header = dataUrl.substring(5, commaIndex)
        val data = dataUrl.substring(commaIndex + 1)
        val mimeType = header.substringBefore(';').ifEmpty {
            "application/octet-stream"
        }

        return mimeType to data
    }

    @RequiresApi(Build.VERSION_CODES.Q)
    private fun saveDataUrlToDownloads(
        dataUrl: String,
        mimeTypeFromListener: String
    ) {
        val extracted = extractMimeTypeAndBase64Data(dataUrl) ?: run {
            Toast.makeText(this, "Invalid data URL", Toast.LENGTH_SHORT).show()
            return
        }

        val (mimeFromData, base64Data) = extracted

        val finalMimeType =
            if (mimeFromData == "application/octet-stream" && mimeTypeFromListener.isNotBlank())
                mimeTypeFromListener
            else
                mimeFromData

        var extension =
            MimeTypeMap.getSingleton().getExtensionFromMimeType(finalMimeType)

        if (!extension.isNullOrEmpty() && !extension.startsWith(".")) {
            extension = ".$extension"
        }

        val sdf = SimpleDateFormat("yyyy-MM-dd_HHmmss", Locale.getDefault())
        val fileName = "${moduleId}_${sdf.format(Date())}${extension ?: ""}"

        try {
            pendingDownloadData = Base64.decode(base64Data, Base64.DEFAULT)
            pendingDownloadSuggestedFilename = fileName

            val intent = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
                addCategory(Intent.CATEGORY_OPENABLE)
                type = finalMimeType
                putExtra(Intent.EXTRA_TITLE, fileName)
            }

            saveFileLauncher.launch(intent)

        } catch (e: Exception) {
            Toast.makeText(
                this,
                "Error preparing file: ${e.message}",
                Toast.LENGTH_LONG
            ).show()
            pendingDownloadData = null
            pendingDownloadSuggestedFilename = null
        }
    }

    fun enableInsets(enable: Boolean = true) {
        runOnUiThread {
            if (isInsetsEnabled != enable) {
                isInsetsEnabled = enable
                ViewCompat.requestApplyInsets(container)
            }
        }
    }

    override fun onResume() {
        super.onResume()
        if (appLockState) {
            val prefs = getSharedPreferences("settings", MODE_PRIVATE)
            val timeout = prefs.getLong("app_lock_timeout", 60000L)

            if (!com.rifsxd.ksunext.ui.util.AppLockManager.shouldPrompt(timeout)) {
                appLockState = false
                if (::lockOverlay.isInitialized) lockOverlay.visibility = android.view.View.GONE
                return
            }

            if (!isPromptShowing) {
                isPromptShowing = true
                lockOverlay.visibility = android.view.View.VISIBLE

                val promptTitle = if (moduleName.isNotBlank()) {
                    getString(com.rifsxd.ksunext.R.string.biometric_prompt_subtitle_module, moduleName)
                } else {
                    getString(com.rifsxd.ksunext.R.string.biometric_prompt_subtitle)
                }

                val promptSubtitle: String? = null

                com.rifsxd.ksunext.ui.util.BiometricAuthenticator(this)
                    .authenticate(
                        title = promptTitle,
                        subtitle = promptSubtitle,
                        onSuccess = {
                            isPromptShowing = false
                            appLockState = false
                            if (::lockOverlay.isInitialized) lockOverlay.visibility = android.view.View.GONE
                            com.rifsxd.ksunext.ui.util.AppLockManager.unlock()
                        },
                        onError = {
                            isPromptShowing = false
                            Toast.makeText(this, "Auth failed: $it", Toast.LENGTH_SHORT).show()
                            finish()
                        }
                    )
            }
        } else {
            if (::lockOverlay.isInitialized) lockOverlay.visibility = android.view.View.GONE
        }
    }

    override fun onStart() {
        super.onStart()
        com.rifsxd.ksunext.ui.util.AppLockManager.onActivityStart()
    }

    override fun onStop() {
        super.onStop()
        com.rifsxd.ksunext.ui.util.AppLockManager.onActivityStop(this)
        val prefs = getSharedPreferences("settings", MODE_PRIVATE)
        if (prefs.getBoolean("enable_biometric_lock", false)) {
            appLockState = true
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putBoolean("appLockState", appLockState)
        outState.putString("moduleName", moduleName)

    }

    override fun onDestroy() {
        webView?.apply {
            stopLoading()
            removeAllViews()
            destroy()
        }
        webView = null
        runCatching {
            webviewInterface.destroy()
            webView?.destroy()
            rootShell?.close()
        }
        super.onDestroy()
    }
}
