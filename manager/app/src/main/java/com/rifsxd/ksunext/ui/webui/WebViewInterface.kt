package com.rifsxd.ksunext.ui.webui

import android.app.Activity
import android.content.Context
import android.content.pm.ApplicationInfo
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.text.TextUtils
import android.util.Base64
import android.util.Log
import android.view.Window
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.widget.Toast
import androidx.core.content.pm.PackageInfoCompat
import androidx.core.graphics.createBitmap
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.rifsxd.ksunext.ui.util.createRootShell
import com.rifsxd.ksunext.ui.util.listModules
import com.rifsxd.ksunext.ui.util.withNewRootShell
import com.rifsxd.ksunext.ui.viewmodel.SuperUserViewModel
import com.topjohnwu.superuser.CallbackList
import com.topjohnwu.superuser.ShellUtils
import com.topjohnwu.superuser.internal.UiThreadHandler
import com.topjohnwu.superuser.io.SuFile
import com.topjohnwu.superuser.io.SuFileInputStream
import com.topjohnwu.superuser.io.SuFileOutputStream
import com.rifsxd.ksunext.ui.util.module.Shortcut
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedOutputStream
import java.io.File
import java.util.UUID
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ConcurrentHashMap

const val TAG = "WebViewInterface"

@Suppress("unused")
class WebViewInterface(
    val context: Context,
    private val webView: WebView,
    private val modDir: String
) {

    @JavascriptInterface
    fun exec(cmd: String): String {
        return withNewRootShell(true) { ShellUtils.fastCmd(this, cmd) }
    }

    @JavascriptInterface
    fun exec(cmd: String, callbackFunc: String) {
        exec(cmd, null, callbackFunc)
    }

    private fun processOptions(sb: StringBuilder, options: String?) {
        val opts = if (options == null) JSONObject() else {
            JSONObject(options)
        }

        val cwd = opts.optString("cwd")
        if (!TextUtils.isEmpty(cwd)) {
            sb.append("cd ${cwd};")
        }

        opts.optJSONObject("env")?.let { env ->
            env.keys().forEach { key ->
                sb.append("export ${key}=${env.getString(key)};")
            }
        }
    }

    @JavascriptInterface
    fun exec(
        cmd: String,
        options: String?,
        callbackFunc: String
    ) {
        val finalCommand = buildString {
            processOptions(this, options)
            append(cmd)
        }

        val result = withNewRootShell(true) {
            newJob().add(finalCommand).to(ArrayList(), ArrayList()).exec()
        }
        val stdout = result.out.joinToString(separator = "\n")
        val stderr = result.err.joinToString(separator = "\n")

        val jsCode =
            "(function() { try { ${callbackFunc}(${result.code}, ${
                JSONObject.quote(
                    stdout
                )
            }, ${JSONObject.quote(stderr)}); } catch(e) { console.error(e); } })();"
        webView.post {
            webView.evaluateJavascript(jsCode, null)
        }
    }

    @JavascriptInterface
    fun spawn(command: String, args: String, options: String?, callbackFunc: String) {
        val finalCommand = buildString {
            processOptions(this, options)

            if (!TextUtils.isEmpty(args)) {
                append(command).append(" ")
                JSONArray(args).let { argsArray ->
                    for (i in 0 until argsArray.length()) {
                        append(argsArray.getString(i))
                        append(" ")
                    }
                }
            } else {
                append(command)
            }
        }

        val shell = createRootShell(true)

        val emitData = fun(name: String, data: String) {
            val jsCode =
                "(function() { try { ${callbackFunc}.${name}.emit('data', ${
                    JSONObject.quote(
                        data
                    )
                }); } catch(e) { console.error('emitData', e); } })();"
            webView.post {
                webView.evaluateJavascript(jsCode, null)
            }
        }

        val stdout = object : CallbackList<String>(UiThreadHandler::runAndWait) {
            override fun onAddElement(s: String) {
                emitData("stdout", s)
            }
        }

        val stderr = object : CallbackList<String>(UiThreadHandler::runAndWait) {
            override fun onAddElement(s: String) {
                emitData("stderr", s)
            }
        }

        val future = shell.newJob().add(finalCommand.toString()).to(stdout, stderr).enqueue()
        val completableFuture = CompletableFuture.supplyAsync {
            future.get()
        }

        completableFuture.thenAccept { result ->
            val emitExitCode =
                "(function() { try { ${callbackFunc}.emit('exit', ${result.code}); } catch(e) { console.error(`emitExit error: \${e}`); } })();"
            webView.post {
                webView.evaluateJavascript(emitExitCode, null)
            }

            if (result.code != 0) {
                val emitErrCode =
                    "(function() { try { var err = new Error(); err.exitCode = ${result.code}; err.message = ${
                        JSONObject.quote(
                            result.err.joinToString(
                                "\n"
                            )
                        )
                    };${callbackFunc}.emit('error', err); } catch(e) { console.error('emitErr', e); } })();"
                webView.post {
                    webView.evaluateJavascript(emitErrCode, null)
                }
            }
        }.whenComplete { _, _ ->
            runCatching { shell.close() }
        }
    }

    @JavascriptInterface
    fun toast(msg: String) {
        webView.post {
            Toast.makeText(context, msg, Toast.LENGTH_SHORT).show()
        }
    }

    @JavascriptInterface
    fun fullScreen(enable: Boolean) {
        if (context is Activity) {
            Handler(Looper.getMainLooper()).post {
                if (enable) {
                    hideSystemUI(context.window)
                } else {
                    showSystemUI(context.window)
                }
            }
        }
    }

    @JavascriptInterface
    fun enableInsets(enable: Boolean = true) {
        if (context is WebUIActivity) {
            context.enableInsets(enable)
        }
    }

    @JavascriptInterface
    fun moduleInfo(): String {
        val moduleInfos = JSONArray(listModules())
        val currentModuleInfo = JSONObject()
        currentModuleInfo.put("moduleDir", modDir)
        val moduleId = File(modDir).getName()
        for (i in 0 until moduleInfos.length()) {
            val currentInfo = moduleInfos.getJSONObject(i)

            if (currentInfo.getString("id") != moduleId) {
                continue
            }

            val keys = currentInfo.keys()
            for (key in keys) {
                currentModuleInfo.put(key, currentInfo.get(key))
            }
            break
        }
        return currentModuleInfo.toString()
    }

    @JavascriptInterface
    fun createShortcut(): Boolean {
        return try {
            val moduleId = File(modDir).name

            val infoJson = JSONObject(moduleInfo())
            val moduleName = infoJson.optString("name", moduleId)
            val webuiIcon = infoJson.optString("webuiIcon").takeIf { it.isNotBlank() }

            fun resolveIcon(p: String?): String? {
                if (p.isNullOrBlank()) return null

                try {
                    val candidate = "/data/adb/modules/$moduleId/$p"
                    val f = SuFile(candidate)
                    if (f.exists()) return "su://$candidate"
                } catch (_: Exception) {
                }

                if (p.startsWith("/")) {
                    try {
                        val f = SuFile(p)
                        if (f.exists()) return "su://$p"
                    } catch (_: Exception) {
                    }
                    return "file://$p"
                }

                return p
            }

            val resolved = resolveIcon(webuiIcon)

            Handler(Looper.getMainLooper()).post {
                Shortcut.createModuleWebUiShortcut(context, moduleId, moduleName, resolved)
            }
            true
        } catch (e: Exception) {
            false
        }
    }

    @JavascriptInterface
    fun listPackages(type: String): String {
        val packageNames = SuperUserViewModel.apps
            .filter { appInfo ->
                val flags = appInfo.packageInfo.applicationInfo?.flags ?: 0
                when (type.lowercase()) {
                    "system" -> (flags and ApplicationInfo.FLAG_SYSTEM) != 0
                    "user" -> (flags and ApplicationInfo.FLAG_SYSTEM) == 0
                    else -> true
                }
            }
            .map { it.packageName }
            .sorted()

        val jsonArray = JSONArray()
        for (pkgName in packageNames) {
            jsonArray.put(pkgName)
        }
        return jsonArray.toString()
    }

    @JavascriptInterface
    fun getPackagesInfo(packageNamesJson: String): String {
        val packageNames = JSONArray(packageNamesJson)
        val jsonArray = JSONArray()
        val appMap = SuperUserViewModel.apps.associateBy { it.packageName }
        for (i in 0 until packageNames.length()) {
            val pkgName = packageNames.getString(i)
            val appInfo = appMap[pkgName]
            if (appInfo != null) {
                val pkg = appInfo.packageInfo
                val app = pkg.applicationInfo
                val obj = JSONObject()
                obj.put("packageName", pkg.packageName)
                obj.put("versionName", pkg.versionName ?: "")
                obj.put("versionCode", PackageInfoCompat.getLongVersionCode(pkg))
                obj.put("appLabel", appInfo.label)
                obj.put("isSystem", if (app != null) ((app.flags and ApplicationInfo.FLAG_SYSTEM) != 0) else JSONObject.NULL)
                obj.put("uid", app?.uid ?: JSONObject.NULL)
                jsonArray.put(obj)
            } else {
                val obj = JSONObject()
                obj.put("packageName", pkgName)
                obj.put("error", "Package not found or inaccessible")
                jsonArray.put(obj)
            }
        }
        return jsonArray.toString()
    }

    private val packageIconCache = HashMap<String, String>()

    @JavascriptInterface
    fun cacheAllPackageIcons(size: Int) {
        val outputStream = java.io.ByteArrayOutputStream()
        SuperUserViewModel.apps.forEach { appInfo ->
            val pkgName = appInfo.packageName
            if (packageIconCache.containsKey(pkgName)) return@forEach
            try {
                SuperUserViewModel.getAppIconDrawable(context, pkgName)?.let { drawable ->
                    val bitmap = drawableToBitmap(drawable, size)
                    outputStream.reset()
                    bitmap.compress(Bitmap.CompressFormat.PNG, 100, outputStream)
                    val byteArray = outputStream.toByteArray()
                    val iconBase64 = "data:image/png;base64," + Base64.encodeToString(byteArray, Base64.NO_WRAP)
                    packageIconCache[pkgName] = iconBase64
                } ?: run {
                     packageIconCache[pkgName] = ""
                }
            } catch (_: Exception) {
                packageIconCache[pkgName] = ""
            }
        }
    }

    @JavascriptInterface
    fun getPackagesIcons(packageNamesJson: String, size: Int): String {
        val packageNames = JSONArray(packageNamesJson)
        val jsonArray = JSONArray()
        val outputStream = java.io.ByteArrayOutputStream()
        for (i in 0 until packageNames.length()) {
            val pkgName = packageNames.getString(i)
            val obj = JSONObject()
            obj.put("packageName", pkgName)
            var iconBase64 = packageIconCache[pkgName]
            if (iconBase64 == null) {
                try {
                    SuperUserViewModel.getAppIconDrawable(context, pkgName)?.let { drawable ->
                        val bitmap = drawableToBitmap(drawable, size)
                        outputStream.reset()
                        bitmap.compress(Bitmap.CompressFormat.PNG, 100, outputStream)
                        val byteArray = outputStream.toByteArray()
                        iconBase64 = "data:image/png;base64," + Base64.encodeToString(byteArray, Base64.NO_WRAP)
                    } ?: run {
                        iconBase64 = ""
                    }
                } catch (_: Exception) {
                    iconBase64 = ""
                }
                packageIconCache[pkgName] = iconBase64 ?: ""
            }
            obj.put("icon", iconBase64)
            jsonArray.put(obj)
        }
        return jsonArray.toString()
    }

     @JavascriptInterface
    fun listFile(path: String): String {
        return try {
            val suFile = SuFile(path)
            val files = suFile.listFiles()?.map { it.name } ?: emptyList()
            JSONArray(files).toString()
        } catch (e: Exception) {
            JSONArray().toString()
        }
    }

    @JavascriptInterface
    fun readFile(path: String): String {
        return try {
            val cmd = "cat '${path.replace("'", "'\\''")}'"
            withNewRootShell(true) { ShellUtils.fastCmd(this, cmd) }
        } catch (e: Exception) {
            ""
        }
    }

    @JavascriptInterface
    fun writeFile(path: String, content: String): Boolean {
        return try {
            val tmpFile = File.createTempFile("webuinext_write", null, context.cacheDir)
            tmpFile.writeText(content)
            val cmd = "cat '${tmpFile.absolutePath.replace("'", "'\\''")}' > '${path.replace("'", "'\\''")}'"
            var result = ""
            withNewRootShell(true) {
                result = ShellUtils.fastCmd(this, cmd)
                this.close()
            }
            tmpFile.delete()
            result.isNotEmpty()
        } catch (e: Exception) {
            false
        }
    }

    @JavascriptInterface
    fun removeFile(path: String): Boolean {
        return try {
            val cmd = "rm -rf '${path.replace("'", "'\\''")}'"
            var result = ""
            withNewRootShell(true) {
                result = ShellUtils.fastCmd(this, cmd)
                this.close()
            }
            result.isNotEmpty()
        } catch (e: Exception) {
            false
        }
    }

    @JavascriptInterface
    fun moveFile(src: String, dest: String): Boolean {
        return try {
            val cmd = "mv '${src.replace("'", "'\\''")}' '${dest.replace("'", "'\\''")}'"
            var result = ""
            withNewRootShell(true) {
                result = ShellUtils.fastCmd(this, cmd)
                this.close()
            }
            result.isNotEmpty()
        } catch (e: Exception) {
            false
        }
    }

    @JavascriptInterface
    fun copyFile(src: String, dest: String): Boolean {
        return try {
            val cmd = "cp -a '${src.replace("'", "'\\''")}' '${dest.replace("'", "'\\''")}'"
            var result = ""
            withNewRootShell(true) {
                result = ShellUtils.fastCmd(this, cmd)
                this.close()
            }
            result.isNotEmpty()
        } catch (e: Exception) {
            false
        }
    }

    val fileOutputStream = FileOutputStreamInterface()

    @JavascriptInterface
    fun fileOutputStream(): FileOutputStreamInterface {
        return fileOutputStream
    }

    fun destroy() {
        fileOutputStream.closeAll()
    }
}

class FileOutputStreamInterface {
    private val openStreams = ConcurrentHashMap<String, BufferedOutputStream>()

    @JavascriptInterface
    fun open(path: String, append: Boolean): String {
        return try {
            val file = SuFile(path)
            val fos = SuFileOutputStream.open(file, append)
            val bos = BufferedOutputStream(fos, 64 * 1024)
            val id = UUID.randomUUID().toString()
            openStreams[id] = bos
            id
        } catch (e: Exception) {
            Log.e(TAG, "open failed", e)
            ""
        }
    }

    @JavascriptInterface
    fun open(path: String): String {
        return open(path, false)
    }

    @JavascriptInterface
    fun writeByte(id: String, b: Int): Boolean {
        return runCatching {
            val bos = openStreams[id] ?: return false
            synchronized(bos) { bos.write(b) }
            true
        }.getOrElse {
            Log.e(TAG, "writeByte failed", it)
            false
        }
    }

    @JavascriptInterface
    fun write(id: String, base64: String): Boolean {
        return runCatching {
            val bos = openStreams[id] ?: return false
            val data = Base64.decode(base64, Base64.NO_WRAP)
            synchronized(bos) { bos.write(data) }
            true
        }.getOrElse {
            Log.e(TAG, "write failed", it)
            false
        }
    }

    @JavascriptInterface
    fun flush(id: String): Boolean {
        return runCatching {
            val bos = openStreams[id] ?: return false
            synchronized(bos) { bos.flush() }
            true
        }.getOrElse {
            Log.e(TAG, "flush failed", it)
            false
        }
    }

    @JavascriptInterface
    fun close(id: String): Boolean {
        val bos = openStreams.remove(id) ?: return false
        return runCatching {
            synchronized(bos) { bos.close() }
            true
        }.getOrElse {
            Log.e(TAG, "close failed", it)
            false
        }
    }

    fun closeAll() {
        openStreams.forEach { (id, bos) ->
            runCatching {
                synchronized(bos) { bos.close() }
            }.onFailure {
                Log.e(TAG, "closeAll failed for $id", it)
            }
        }
        openStreams.clear()
    }
}

fun drawableToBitmap(drawable: Drawable, size: Int): Bitmap {
    if (drawable is BitmapDrawable && drawable.bitmap.width == size && drawable.bitmap.height == size) {
        return drawable.bitmap
    }
    val bitmap = createBitmap(size, size)
    val canvas = Canvas(bitmap)
    drawable.setBounds(0, 0, size, size)
    drawable.draw(canvas)
    return bitmap
}

fun hideSystemUI(window: Window) =
    WindowInsetsControllerCompat(window, window.decorView).let { controller ->
        controller.hide(WindowInsetsCompat.Type.systemBars())
        controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
    }

fun showSystemUI(window: Window) =
    WindowInsetsControllerCompat(window, window.decorView).show(WindowInsetsCompat.Type.systemBars())