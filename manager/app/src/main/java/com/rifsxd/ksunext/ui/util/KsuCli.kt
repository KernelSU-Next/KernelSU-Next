package com.rifsxd.ksunext.ui.util

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
import com.topjohnwu.superuser.CallbackList
import com.topjohnwu.superuser.Shell
import com.topjohnwu.superuser.ShellUtils
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.parcelize.Parcelize
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ksuApp
import org.json.JSONArray
import java.io.File

/**
 * @author weishu
 * @date 2023/1/1.
 */
private const val TAG = "KsuCli"
private const val BUSYBOX = "/data/adb/ksu/bin/busybox"

private val ksuDaemonMagicPath by lazy {
    "${ksuApp.applicationInfo.nativeLibraryDir}${File.separator}libksud_magic.so"
}

private val ksuDaemonOverlayfsPath by lazy {
    "${ksuApp.applicationInfo.nativeLibraryDir}${File.separator}libksud_overlayfs.so"
}

fun readMountSystemFile(): Boolean {
    val filePath = "/data/adb/ksu/mount_system"
    val result = ShellUtils.fastCmd("cat $filePath").trim()
    return result == "OVERLAYFS"
}

// Get the path based on the user's choice
fun getKsuDaemonPath(): String {
    val useOverlayFs = readMountSystemFile()
    
    return if (useOverlayFs) {
        ksuDaemonOverlayfsPath
    } else {
        ksuDaemonMagicPath
    }
}

fun updateMountSystemFile(useOverlayFs: Boolean) {
    val filePath = "/data/adb/ksu/mount_system"
    if (useOverlayFs) {
        ShellUtils.fastCmd("echo -n OVERLAYFS > $filePath")
    } else {
        ShellUtils.fastCmd("echo -n MAGIC_MOUNT > $filePath")
    }
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
        if (globalMnt) {
            setCommands(ksuDaemonMagicPath, "debug", "su", "-g")
        } else {
            setCommands(ksuDaemonMagicPath, "debug", "su")
        }
    }
}

fun createRootShell(globalMnt: Boolean = false): Shell {
    return runCatching {
        createRootShellBuilder(globalMnt).build()
    }.getOrElse { e ->
        Log.w(TAG, "ksu failed: ", e)
        Shell.Builder.create().run {
            runCatching {
                if (globalMnt) {
                    setFlags(Shell.FLAG_MOUNT_MASTER)
                }
                build()
            }.getOrElse { e ->
                Log.e(TAG, "su failed: ", e)
                build("sh")
            }
        }
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
    val result = listModules()
    runCatching {
        val array = JSONArray(result)
        return array.length()
    }.getOrElse { return 0 }
}

fun getSuperuserCount(): Int {
    return Natives.allowList.size
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

suspend fun shrinkModules(): Boolean = withContext(Dispatchers.IO) {
    execKsud("module shrink", true)
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
        ShellUtils.fastCmdResult("/system/bin/reboot $reason")
    }
    ShellUtils.fastCmdResult("/system/bin/svc power reboot $reason || /system/bin/reboot $reason")
}

fun rootAvailable() = Shell.isAppGrantedRoot() == true

fun isAbDevice(): Boolean {
    return ShellUtils.fastCmd("getprop ro.build.ab_update").trim().toBoolean()
}

fun isInitBoot(): Boolean {
    return !Os.uname().release.contains("android12-")
}

suspend fun getCurrentKmi(): String = withContext(Dispatchers.IO) {
    val cmd = "boot-info current-kmi"
    ShellUtils.fastCmd("${getKsuDaemonPath()} $cmd")
}

suspend fun getSupportedKmis(): List<String> = withContext(Dispatchers.IO) {
    val cmd = "boot-info supported-kmi"
    val out = Shell.cmd("${getKsuDaemonPath()} $cmd").to(ArrayList(), null).exec().out
    out.filter { it.isNotBlank() }.map { it.trim() }
}

fun overlayFsAvailable(): Boolean {
    // check /proc/filesystems
    return ShellUtils.fastCmdResult("cat /proc/filesystems | grep overlay")
}

fun hasMagisk(): Boolean {
    val result = Shell.cmd("which magisk").exec()
    Log.i(TAG, "has magisk: ${result.isSuccess}")
    return result.isSuccess
}

fun isGlobalNamespaceEnabled(): Boolean {
    val result =
        ShellUtils.fastCmd("nsenter --mount=/proc/1/ns/mnt cat ${Natives.GLOBAL_NAMESPACE_FILE}")
    Log.i(TAG, "is global namespace enabled: $result")
    return result == "1"
}

fun setGlobalNamespaceEnabled(value: String) {
    Shell.cmd("nsenter --mount=/proc/1/ns/mnt echo $value > ${Natives.GLOBAL_NAMESPACE_FILE}")
        .submit { result ->
            Log.i(TAG, "setGlobalNamespaceEnabled result: ${result.isSuccess} [${result.out}]")
        }
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
    val resultBase = ShellUtils.fastCmd("mkdir -p $baseBackupDir").trim()
    if (resultBase.isNotEmpty()) return null

    val timestamp = ShellUtils.fastCmd("date +%Y%m%d_%H%M%S").trim()
    if (timestamp.isEmpty()) return null

    val newBackupDir = "$baseBackupDir/$timestamp"
    val resultNewDir = ShellUtils.fastCmd("mkdir -p $newBackupDir").trim()

    if (resultNewDir.isEmpty()) return newBackupDir
    return null
}

fun moduleBackup(): Boolean {
    val checkEmptyCommand = "if [ -z \"$(ls -A /data/adb/modules)\" ]; then echo 'empty'; fi"
    val resultCheckEmpty = ShellUtils.fastCmd(checkEmptyCommand).trim()
    if (resultCheckEmpty == "empty") {
        return false
    }

    val timestamp = ShellUtils.fastCmd("date +%Y%m%d_%H%M%S").trim()
    if (timestamp.isEmpty()) return false

    val tarName = "modules_backup_$timestamp.tar"
    val tarPath = "/data/local/tmp/$tarName"
    val internalBackupDir = "/data/adb/ksu/backup/modules"
    val internalBackupPath = "$internalBackupDir/$tarName"

    val tarCmd = "$BUSYBOX tar -cpf $tarPath -C /data/adb/modules $(ls /data/adb/modules)"
    val tarResult = ShellUtils.fastCmd(tarCmd).trim()
    if (tarResult.isNotEmpty()) return false

    ShellUtils.fastCmd("mkdir -p $internalBackupDir")

    val cpResult = ShellUtils.fastCmd("cp $tarPath $internalBackupPath").trim()
    if (cpResult.isNotEmpty()) return false

    ShellUtils.fastCmd( "rm -f $tarPath")

    return true
}

fun moduleRestore(): Boolean {
    val findTarCmd = "ls -t /data/adb/ksu/backup/modules/modules_backup_*.tar 2>/dev/null | head -n 1"
    val tarPath = ShellUtils.fastCmd(findTarCmd).trim()
    if (tarPath.isEmpty()) return false

    val extractCmd = "$BUSYBOX tar -xpf $tarPath -C /data/adb/modules_update"
    val extractResult = ShellUtils.fastCmd(extractCmd).trim()
    return extractResult.isEmpty()
}

fun allowlistBackup(): Boolean {
    val checkEmptyCommand = "if [ -z \"$(ls -A /data/adb/ksu/.allowlist)\" ]; then echo 'empty'; fi"
    val resultCheckEmpty = ShellUtils.fastCmd(checkEmptyCommand).trim()
    if (resultCheckEmpty == "empty") {
        return false
    }

    val timestamp = ShellUtils.fastCmd("date +%Y%m%d_%H%M%S").trim()
    if (timestamp.isEmpty()) return false

    val tarName = "allowlist_backup_$timestamp.tar"
    val tarPath = "/data/local/tmp/$tarName"
    val internalBackupDir = "/data/adb/ksu/backup/allowlist"
    val internalBackupPath = "$internalBackupDir/$tarName"

    val tarCmd = "$BUSYBOX tar -cpf $tarPath -C /data/adb/ksu .allowlist"
    val tarResult = ShellUtils.fastCmd(tarCmd).trim()
    if (tarResult.isNotEmpty()) return false

    ShellUtils.fastCmd("mkdir -p $internalBackupDir")

    val cpResult = ShellUtils.fastCmd("cp $tarPath $internalBackupPath").trim()
    if (cpResult.isNotEmpty()) return false

    ShellUtils.fastCmd("rm -f $tarPath")

    return true
}

fun allowlistRestore(): Boolean {
    // Find the latest allowlist tar backup in /data/adb/ksu/backup/allowlist
    val findTarCmd = "ls -t /data/adb/ksu/backup/allowlist/allowlist_backup_*.tar 2>/dev/null | head -n 1"
    val tarPath = ShellUtils.fastCmd(findTarCmd).trim()
    if (tarPath.isEmpty()) return false

    // Extract the tar to /data/adb/ksu (restores .allowlist folder with permissions)
    val extractCmd = "$BUSYBOX tar -xpf $tarPath -C /data/adb/ksu"
    val extractResult = ShellUtils.fastCmd(extractCmd).trim()
    return extractResult.isEmpty()
}

fun moduleMigration(): Boolean {
    val command = "cp -rp /data/adb/modules/* /data/adb/modules_update"
    val result = ShellUtils.fastCmd(command).trim()

    return result.isEmpty()
}

private val suSFSDaemonPath by lazy {
    "${ksuApp.applicationInfo.nativeLibraryDir}${File.separator}libsusfsd.so"
}

fun getSuSFS(): String {
    return ShellUtils.fastCmd("$suSFSDaemonPath support")
}

fun getSuSFSVersion(): String {
    val result = ShellUtils.fastCmd("$suSFSDaemonPath version")
    return result
}

fun getSuSFSVariant(): String {
    return ShellUtils.fastCmd("$suSFSDaemonPath variant")
}

fun getSuSFSFeatures(): String {
    return ShellUtils.fastCmd("$suSFSDaemonPath features")
}

fun hasSuSFs_SUS_SU(): String {
    return ShellUtils.fastCmd("$suSFSDaemonPath sus_su support")
}

fun susfsSUS_SU_0(): String {
    return ShellUtils.fastCmd("$suSFSDaemonPath sus_su 0")
}

fun susfsSUS_SU_2(): String {
    return ShellUtils.fastCmd("$suSFSDaemonPath sus_su 2")
}

fun susfsSUS_SU_Mode(): String {
    return ShellUtils.fastCmd("$suSFSDaemonPath sus_su mode")
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
    return Natives.version >= Natives.MINIMAL_SUPPORTED_SU_COMPAT && !Natives.isSuEnabled()
}

fun zygiskRequired(dir: File): Boolean {
    val zygiskLib = "${dir.absolutePath}/zygisk"
    val cmd = "ls \"$zygiskLib\""
    val result = ShellUtils.fastCmdResult(cmd)
    return result
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
