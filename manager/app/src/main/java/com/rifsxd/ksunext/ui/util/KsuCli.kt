package com.rifsxd.ksunext.ui.util

import android.app.Activity
import android.content.Intent
import android.content.ContentResolver
import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.os.Environment
import android.os.Parcelable
import android.os.SystemClock
import android.provider.OpenableColumns
import android.system.Os
import android.util.Log
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ksuApp
import com.topjohnwu.superuser.CallbackList
import com.topjohnwu.superuser.Shell
import com.topjohnwu.superuser.ShellUtils
import com.topjohnwu.superuser.io.SuFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.parcelize.Parcelize
import org.json.JSONArray
import java.io.File
import java.text.SimpleDateFormat
import java.util.*

/**
 * @author weishu
 * @date 2023/1/1.
 */
private const val TAG = "KsuCli"
private const val BUSYBOX = "/data/adb/ksu/bin/busybox"

private fun getKsuDaemonPath(): String {
    return ksuApp.applicationInfo.nativeLibraryDir + File.separator + "libksud.so"
}

data class FlashResult(val code: Int, val err: String, val showReboot: Boolean) {
    constructor(result: Shell.Result, showReboot: Boolean) : this(result.code, result.err.joinToString("\n"), showReboot)
    constructor(result: Shell.Result) : this(result, result.isSuccess)
}

inline fun <T> withNewRootShell(
    globalMnt: Boolean = false,
    block: Shell.() -> T
): T {
    return createRootShell(globalMnt).use(block)
}

fun Uri.getFileName(context: Context): String? {
    var fileName: String? = null
    val contentResolver: ContentResolver = context.contentResolver
    val cursor: Cursor? = contentResolver.query(this, null, null, null, null)
    cursor?.use {
        if (it.moveToFirst()) {
            fileName = it.getString(it.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
        }
    }
    return fileName
}

fun createRootShellBuilder(globalMnt: Boolean = false): Shell.Builder {
    return Shell.Builder.create().run {
        val cmd = buildString {
            append("${getKsuDaemonPath()} debug su")
            if (globalMnt) append(" -g")
            append(" || ")
            append("su")
            if (globalMnt) append(" --mount-master")
            append(" || ")
            append("sh")
        }
        setCommands("sh", "-c", cmd)
    }
}

fun createRootShell(globalMnt: Boolean = false): Shell {
    return runCatching {
        createRootShellBuilder(globalMnt).build()
    }.getOrElse { e ->
        Log.w(TAG, "su failed: ", e)
        Shell.Builder.create().apply {
            if (globalMnt) setFlags(Shell.FLAG_MOUNT_MASTER)
        }.build()
    }
}

fun execKsud(args: String, newShell: Boolean = false): Boolean {
    return if (newShell) {
        withNewRootShell {
            ShellUtils.fastCmdResult(this, "${getKsuDaemonPath()} $args")
        }
    } else {
        ShellUtils.fastCmdResult("${getKsuDaemonPath()} $args")
    }
}

suspend fun getFeatureStatus(feature: String): String = withContext(Dispatchers.IO) {
    val shell = createRootShell(true)
    
    val out = shell.newJob()
        .add("${getKsuDaemonPath()} feature check $feature").to(ArrayList<String>(), null).exec().out
    out.firstOrNull()?.trim().orEmpty()
}

suspend fun getFeaturePersistValue(feature: String): Long? = withContext(Dispatchers.IO) {
    val shell = createRootShell(true)
    
    val out = shell.newJob()
        .add("${getKsuDaemonPath()} feature get --config $feature").to(ArrayList<String>(), null).exec().out
    val valueLine = out.firstOrNull { it.trim().startsWith("Value:") } ?: return@withContext null
    valueLine.substringAfter("Value:").trim().toLongOrNull()
}

fun install() {
    val start = SystemClock.elapsedRealtime()
    val magiskboot = File(ksuApp.applicationInfo.nativeLibraryDir, "libmagiskboot.so").absolutePath
    val result = execKsud("install --magiskboot $magiskboot", true)
    Log.w(TAG, "install result: $result, cost: ${SystemClock.elapsedRealtime() - start}ms")
}

fun listModules(): String {
    val out =
        Shell.cmd("${getKsuDaemonPath()} module list").to(ArrayList(), null).exec().out
    return out.joinToString("\n").ifBlank { "[]" }
}

fun getModuleCount(): Int {
    return runCatching {
        JSONArray(listModules()).length()
    }.getOrDefault(0)
}

fun getSuperuserCount(): Int {
    return Natives.getSuperuserCount()
}

fun toggleModule(id: String, enable: Boolean): Boolean {
    val cmd = if (enable) {
        "module enable $id"
    } else {
        "module disable $id"
    }
    val result = execKsud(cmd, true)
    Log.i(TAG, "$cmd result: $result")
    return result
}

fun uninstallModule(id: String): Boolean {
    val cmd = "module uninstall $id"
    val result = execKsud(cmd, true)
    Log.i(TAG, "uninstall module $id result: $result")
    return result
}

fun restoreModule(id: String): Boolean {
    val cmd = "module restore $id"
    val result = execKsud(cmd, true)
    Log.i(TAG, "restore module $id result: $result")
    return result
}

fun getSelinuxEnforce(): Boolean? {
    return runCatching {
        val out = ShellUtils.fastCmd("getenforce").trim()
        when {
            out.equals("Enforcing", ignoreCase = true) -> true
            out.equals("Permissive", ignoreCase = true) -> false
            else -> null
        }
    }.getOrNull()
}

fun setSelinuxEnforce(enforce: Boolean): Boolean {
    return runCatching {
        val valStr = if (enforce) "1" else "0"
        ShellUtils.fastCmdResult("setenforce $valStr")
    }.getOrDefault(false)
}

private fun processUiPrintLine(s: String?): Pair<Int, String?> {
    if (s == null) {
        return Pair(1,null)
    }

    val check1 = s.startsWith("ui_print")
    val trimmed = s.trim()
    val check2 = trimmed.startsWith("ui_print")
    if (!check1 && check2) return Pair(1,null)

    return if(check1) {
        Pair(1,trimmed.drop(8).dropWhile { it.isWhitespace() })
    }
    else {
        Pair(2, trimmed)
    }
}

private fun flashWithIO_ak3(
    cmd: String,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit
): Shell.Result {

    val stdoutCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            val (type, text) = processUiPrintLine(s)
            if(type == 1) {
                text?.let(onStdout)
            } else {
                text?.let(onStderr)
            }
        }
    }

    val stderrCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStderr(s ?: "")
        }
    }

    return withNewRootShell {
        newJob().add(cmd).to(stdoutCallback, stderrCallback).exec()
    }
}

private fun flashWithIO(
    cmd: String,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit
): Shell.Result {

    val stdoutCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStdout(s ?: "")
        }
    }

    val stderrCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStderr(s ?: "")
        }
    }

    return withNewRootShell {
        newJob().add(cmd).to(stdoutCallback, stderrCallback).exec()
    }
}

fun flashModule(
    uri: Uri,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit
): FlashResult {
    val resolver = ksuApp.contentResolver
    with(resolver.openInputStream(uri)) {
        val file = File(ksuApp.cacheDir, "module.zip")
        file.outputStream().use { output ->
            this?.copyTo(output)
        }
        val cmd = "module install ${file.absolutePath}"
        val result = flashWithIO("${getKsuDaemonPath()} $cmd", onStdout, onStderr)
        Log.i("KernelSU", "install module $uri result: $result")

        file.delete()

        return FlashResult(result)
    }
}

fun runModuleAction(
    moduleId: String, onStdout: (String) -> Unit, onStderr: (String) -> Unit
): Boolean {
    val shell = createRootShell(true)

    val stdoutCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStdout(s ?: "")
        }
    }

    val stderrCallback: CallbackList<String?> = object : CallbackList<String?>() {
        override fun onAddElement(s: String?) {
            onStderr(s ?: "")
        }
    }

    val result = shell.newJob().add("${getKsuDaemonPath()} module action $moduleId")
        .to(stdoutCallback, stderrCallback).exec()
    Log.i("KernelSU", "Module runAction result: $result")

    return result.isSuccess
}

fun restoreBoot(
    onStdout: (String) -> Unit, onStderr: (String) -> Unit
): FlashResult {
    val magiskboot = File(ksuApp.applicationInfo.nativeLibraryDir, "libmagiskboot.so")
    val result = flashWithIO("${getKsuDaemonPath()} boot-restore -f --magiskboot $magiskboot", onStdout, onStderr)
    return FlashResult(result)
}

fun uninstallPermanently(
    onStdout: (String) -> Unit, onStderr: (String) -> Unit
): FlashResult {
    val magiskboot = File(ksuApp.applicationInfo.nativeLibraryDir, "libmagiskboot.so")
    val result = flashWithIO("${getKsuDaemonPath()} uninstall --magiskboot $magiskboot", onStdout, onStderr)
    return FlashResult(result)
}

@Parcelize
sealed class LkmSelection : Parcelable {
    data class LkmUri(val uri: Uri) : LkmSelection()
    data class KmiString(val value: String) : LkmSelection()
    data object KmiNone : LkmSelection()
}

fun installBoot(
    bootUri: Uri?,
    lkm: LkmSelection,
    ota: Boolean,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit,
): FlashResult {
    val resolver = ksuApp.contentResolver

    val bootFile = bootUri?.let { uri ->
        with(resolver.openInputStream(uri)) {
            val bootFile = File(ksuApp.cacheDir, "boot.img")
            bootFile.outputStream().use { output ->
                this?.copyTo(output)
            }

            bootFile
        }
    }

    val magiskboot = File(ksuApp.applicationInfo.nativeLibraryDir, "libmagiskboot.so")
    var cmd = "boot-patch --magiskboot ${magiskboot.absolutePath}"

    cmd += if (bootFile == null) {
        // no boot.img, use -f to force install
        " -f"
    } else {
        " -b ${bootFile.absolutePath}"
    }

    if (ota) {
        cmd += " -u"
    }

    var lkmFile: File? = null
    when (lkm) {
        is LkmSelection.LkmUri -> {
            lkmFile = with(resolver.openInputStream(lkm.uri)) {
                val file = File(ksuApp.cacheDir, "kernelsu-tmp-lkm.ko")
                file.outputStream().use { output ->
                    this?.copyTo(output)
                }

                file
            }
            cmd += " -m ${lkmFile.absolutePath}"
        }

        is LkmSelection.KmiString -> {
            cmd += " --kmi ${lkm.value}"
        }

        LkmSelection.KmiNone -> {
            // do nothing
        }
    }

    // output dir
    val downloadsDir =
        Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
    cmd += " -o $downloadsDir"

    val result = flashWithIO("${getKsuDaemonPath()} $cmd", onStdout, onStderr)
    Log.i("KernelSU", "install boot result: ${result.isSuccess}")

    bootFile?.delete()
    lkmFile?.delete()

    // if boot uri is empty, it is direct install, when success, we should show reboot button
    return FlashResult(result, bootUri == null && result.isSuccess)
}

fun reboot(reason: String = "") {
    if (reason == "recovery") {
        // KEYCODE_POWER = 26, hide incorrect "Factory data reset" message
        ShellUtils.fastCmdResult("/system/bin/input keyevent 26")
    }
    ShellUtils.fastCmdResult("/system/bin/svc power reboot $reason || /system/bin/reboot $reason")
}

fun flashAnyKernelZip(
    uri: Uri,
    onStdout: (String) -> Unit,
    onStderr: (String) -> Unit
): FlashResult {
    val resolver = ksuApp.contentResolver

    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
    val tmpFile = File(ksuApp.cacheDir, "anykernel_${timestamp}.zip")
    resolver.openInputStream(uri).use { input ->
        tmpFile.outputStream().use { out ->
            input?.copyTo(out)
        }
    }

    val destZip = tmpFile.absolutePath
    val destZipName = File(destZip).name
    val destDirFile = File(ksuApp.cacheDir, "anykernel3_${timestamp}")
    val destDir = destDirFile.absolutePath

    // Validate that the zip contains the required META-INF installer script
    val hasInstaller = runCatching {
        java.util.zip.ZipFile(tmpFile).use { zip ->
            zip.getEntry("META-INF/com/google/android/update-binary") != null
        }
    }.getOrDefault(false)

    if (!hasInstaller) {
        tmpFile.delete()
        val errMsg = "Invalid AnyKernel3 zip: META-INF/com/google/android/update-binary not found!"
        onStderr(errMsg)
        return FlashResult(1, "", false)
    }

    val cmd = """
                mkdir -p '$destDir' && \
                $BUSYBOX unzip -p -o '$destZip' "META-INF/com/google/android/update-binary" > '$destDir/update-binary' 2>/dev/null && \
                cp '$destZip' '$destDir/$destZipName' 2>/dev/null || true && \
                $BUSYBOX chmod 755 '$destDir/update-binary' && \
                $BUSYBOX chown root:root '$destDir/update-binary' && \
                (cd '$destDir' && \
                    if [ -f './update-binary' ]; then \
                        AKHOME='$destDir/tmp' $BUSYBOX ash '$destDir/update-binary' 3 1 '$destDir/$destZipName'; \
                    else \
                        echo 'No installer script found' >&2; exit 1; \
                    fi)
            """.trimIndent().replace(Regex("\\s+\\\\\\s*"), " ")

    val result = flashWithIO_ak3(cmd, onStdout, onStderr)
    try {
        return FlashResult(result, result.isSuccess)
    } finally {
        try {
            runCatching {
                createRootShell(true).use { sh ->
                    sh.newJob().add("rm -rf '$destDir' '$destZip'").exec()
                }
            }
        } catch (_: Throwable) {
        }
    }
}

fun rootAvailable() = Shell.isAppGrantedRoot() == true

fun isInitBoot(): Boolean {
    return !Os.uname().release.contains("android12-")
}

suspend fun getCurrentKmi(): String = withContext(Dispatchers.IO) {
    val cmd = "boot-info current-kmi"
    ShellUtils.fastCmd("${getKsuDaemonPath()} $cmd")
}

suspend fun getSupportedKmis(): List<String> = withContext(Dispatchers.IO) {
    val cmd = "boot-info supported-kmis"
    val out = Shell.cmd("${getKsuDaemonPath()} $cmd").to(ArrayList(), null).exec().out
    out.filter { it.isNotBlank() }.map { it.trim() }
}

suspend fun isAbDevice(): Boolean = withContext(Dispatchers.IO) {
    val cmd = "boot-info is-ab-device"
    ShellUtils.fastCmd("${getKsuDaemonPath()} $cmd").trim().toBoolean()
}

suspend fun getDefaultPartition(): String = withContext(Dispatchers.IO) {
    if (rootAvailable()) {
        val cmd = "boot-info default-partition"
        ShellUtils.fastCmd("${getKsuDaemonPath()} $cmd").trim()
    } else {
        if (!Os.uname().release.contains("android12-")) "init_boot" else "boot"
    }
}

suspend fun getSlotSuffix(ota: Boolean): String = withContext(Dispatchers.IO) {
    val cmd = if (ota) {
        "boot-info slot-suffix --ota"
    } else {
        "boot-info slot-suffix"
    }
    ShellUtils.fastCmd("${getKsuDaemonPath()} $cmd").trim()
}

suspend fun getAvailablePartitions(): List<String> = withContext(Dispatchers.IO) {
    val shell = createRootShell(true)
    val cmd = "boot-info available-partitions"
    val out = shell.newJob().add("${getKsuDaemonPath()} $cmd").to(ArrayList(), null).exec().out
    out.filter { it.isNotBlank() }.map { it.trim() }
}

fun hasMagisk(): Boolean {
    val result = ShellUtils.fastCmdResult("which magisk")
    Log.i(TAG, "has magisk: $result")
    return result
}

fun isSepolicyValid(rules: String?): Boolean {
    if (rules == null) {
        return true
    }
    val result =
        Shell.cmd("${getKsuDaemonPath()} sepolicy check '$rules'").to(ArrayList(), null)
            .exec()
    return result.isSuccess
}

fun getSepolicy(pkg: String): String {
    val result =
        Shell.cmd("${getKsuDaemonPath()} profile get-sepolicy $pkg").to(ArrayList(), null)
            .exec()
    Log.i(TAG, "code: ${result.code}, out: ${result.out}, err: ${result.err}")
    return result.out.joinToString("\n")
}

fun setSepolicy(pkg: String, rules: String): Boolean {
    val result = Shell.cmd("${getKsuDaemonPath()} profile set-sepolicy $pkg '$rules'")
        .to(ArrayList(), null).exec()
    Log.i(TAG, "set sepolicy result: ${result.code}")
    return result.isSuccess
}

fun listAppProfileTemplates(): List<String> {
    return Shell.cmd("${getKsuDaemonPath()} profile list-templates").to(ArrayList(), null)
        .exec().out
}

fun getAppProfileTemplate(id: String): String {
    return Shell.cmd("${getKsuDaemonPath()} profile get-template '${id}'")
        .to(ArrayList(), null).exec().out.joinToString("\n")
}

fun getFileName(context: Context, uri: Uri): String {
    var name = "Unknown Module"
    if (uri.scheme == ContentResolver.SCHEME_CONTENT) {
        val cursor: Cursor? = context.contentResolver.query(uri, null, null, null, null)
        cursor?.use {
            if (it.moveToFirst()) {
                name = it.getString(it.getColumnIndexOrThrow(OpenableColumns.DISPLAY_NAME))
            }
        }
    } else if (uri.scheme == "file") {
        name = uri.lastPathSegment ?: "Unknown Module"
    }
    return name
}

fun moduleBackupDir(): String? {
    val baseBackupDir = "/data/adb/ksu/backup/modules"

    if (!SuFile(baseBackupDir).mkdirs()) return null

    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())

    val newBackupDir = "$baseBackupDir/$timestamp"

    if (SuFile(newBackupDir).mkdirs()) return newBackupDir
    return null
}

fun moduleBackup(): Boolean {
    if (SuFile("/data/adb/modules").listFiles()?.isEmpty() ?: true) return false

    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())

    val tarName = "modules_backup_$timestamp.tar"
    val tarPath = "/data/local/tmp/$tarName"
    val internalBackupDir = "/data/adb/ksu/backup/modules"
    val internalBackupPath = "$internalBackupDir/$tarName"

    val tarCmd = "$BUSYBOX tar -cpf $tarPath -C /data/adb/modules $(ls /data/adb/modules)"
    val tarResult = ShellUtils.fastCmdResult(tarCmd)
    if (!tarResult) return false

    if (!SuFile(internalBackupDir).mkdirs()) return false

    val cpResult = ShellUtils.fastCmdResult("cp $tarPath $internalBackupPath")
    if (!cpResult) return false

    SuFile(tarPath).delete()

    return true
}

fun moduleRestore(): Boolean {
    val findTarCmd = "ls -t /data/adb/ksu/backup/modules/modules_backup_*.tar 2>/dev/null | head -n 1"
    val tarPath = ShellUtils.fastCmd(findTarCmd).trim()
    if (tarPath.isEmpty()) return false

    val extractCmd = "$BUSYBOX tar -xpf $tarPath -C /data/adb/modules_update"
    return ShellUtils.fastCmdResult(extractCmd)
}

fun allowlistBackup(): Boolean {
    if (!SuFile("/data/adb/ksu/.allowlist").exists()) return false

    val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())

    val tarName = "allowlist_backup_$timestamp.tar"
    val tarPath = "/data/local/tmp/$tarName"
    val internalBackupDir = "/data/adb/ksu/backup/allowlist"
    val internalBackupPath = "$internalBackupDir/$tarName"

    val tarCmd = "$BUSYBOX tar -cpf $tarPath -C /data/adb/ksu .allowlist"
    val tarResult = ShellUtils.fastCmdResult(tarCmd)
    if (!tarResult) return false

    if (!SuFile(internalBackupDir).mkdirs()) return false

    val cpResult = ShellUtils.fastCmdResult("cp $tarPath $internalBackupPath")
    if (!cpResult) return false

    SuFile(tarPath).delete()

    return true
}

fun allowlistRestore(): Boolean {
    // Find the latest allowlist tar backup in /data/adb/ksu/backup/allowlist
    val findTarCmd = "ls -t /data/adb/ksu/backup/allowlist/allowlist_backup_*.tar 2>/dev/null | head -n 1"
    val tarPath = ShellUtils.fastCmd(findTarCmd).trim()
    if (tarPath.isEmpty()) return false

    // Extract the tar to /data/adb/ksu (restores .allowlist folder with permissions)
    val extractCmd = "$BUSYBOX tar -xpf $tarPath -C /data/adb/ksu"
    return ShellUtils.fastCmdResult(extractCmd)
}

fun moduleMigration(): Boolean {
    val command = "cp -rp /data/adb/modules/* /data/adb/modules_update"
    return ShellUtils.fastCmdResult(command)
}

fun currentMountSystem(): String {
    val result = ShellUtils.fastCmd("${getKsuDaemonPath()} module mount").trim()
    return result.substringAfter(":").substringAfter(" ").trim()
}

fun getModuleSize(dir: File): Long {
    val result = ShellUtils.fastCmd("$BUSYBOX du -sb '${dir.absolutePath}' | awk '{print $1}'").trim()
    return result.toLongOrNull() ?: 0L
}

fun isSuCompatDisabled(): Boolean {
    return !Natives.isSuEnabled()
}

fun zygiskRequired(dir: File): Boolean {
    return (SuFile(dir, "zygisk").listFiles()?.size ?: 0) > 0
}

fun getZygiskImplementation(property: String): String {
    val modulesPath = "/data/adb/modules"
    val zygiskModuleIds = arrayOf("rezygisk", "zygisksu")

    for (moduleId in zygiskModuleIds) {
        val moduleDir = SuFile.open("$modulesPath/$moduleId")
        if (!moduleDir.isDirectory) continue
        if (SuFile.open("$modulesPath/$moduleId/disable").isFile ||
            SuFile.open("$modulesPath/$moduleId/remove").isFile
        ) continue

        val propFile = SuFile.open("$modulesPath/$moduleId/module.prop")
        if (!propFile.isFile) continue

        val prop = Properties().apply { load(propFile.newInputStream()) }
        prop.getProperty(property)?.let {
            Log.i(TAG, "Zygisk $property: $it")
            return it
        }
    }

    Log.i(TAG, "Zygisk $property: None")
    return "None"
}

fun refreshActivity(context: Context) {
    if (context is Activity) {
        context.recreate()
    }
}

fun restartActivity(context: Context) {
    val packageManager = context.packageManager
    val intent = packageManager.getLaunchIntentForPackage(context.packageName)
    intent?.addFlags(
        Intent.FLAG_ACTIVITY_NEW_TASK or
        Intent.FLAG_ACTIVITY_CLEAR_TASK or
        Intent.FLAG_ACTIVITY_CLEAR_TOP
    )
    context.startActivity(intent)
    if (context is Activity) {
        context.finish()
    }
}

fun setAppProfileTemplate(id: String, template: String): Boolean {
    val escapedTemplate = template.replace("\"", "\\\"")
    val cmd = """${getKsuDaemonPath()} profile set-template "$id" "$escapedTemplate'""""
    return Shell.cmd(cmd)
        .to(ArrayList(), null).exec().isSuccess
}

fun deleteAppProfileTemplate(id: String): Boolean {
    return Shell.cmd("${getKsuDaemonPath()} profile delete-template '${id}'")
        .to(ArrayList(), null).exec().isSuccess
}

fun forceStopApp(packageName: String) {
    val result = Shell.cmd("am force-stop $packageName").exec()
    Log.i(TAG, "force stop $packageName result: $result")
}

fun launchApp(packageName: String) {
    val result =
        Shell.cmd("cmd package resolve-activity --brief $packageName | tail -n 1 | xargs cmd activity start-activity -n")
            .exec()
    Log.i(TAG, "launch $packageName result: $result")
}

fun restartApp(packageName: String) {
    forceStopApp(packageName)
    launchApp(packageName)
}
