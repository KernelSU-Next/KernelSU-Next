import com.android.build.api.dsl.ApplicationDefaultConfig
import com.android.build.api.dsl.CommonExtension
import com.android.build.gradle.api.AndroidBasePlugin
import java.io.ByteArrayOutputStream

plugins {
    alias(libs.plugins.agp.app) apply false
    alias(libs.plugins.agp.lib) apply false
    alias(libs.plugins.kotlin) apply false
    alias(libs.plugins.compose.compiler) apply false
    alias(libs.plugins.lsplugin.cmaker)
}

cmaker {
    default {
        arguments.addAll(
            arrayOf(
                "-DANDROID_STL=none",
            )
        )
        abiFilters("arm64-v8a", "x86_64", "riscv64")
    }
    buildTypes {
        if (it.name == "release") {
            arguments += "-DDEBUG_SYMBOLS_PATH=${layout.buildDirectory.asFile.get().absolutePath}/symbols"
        }
    }
}

val androidMinSdkVersion = 26
val androidTargetSdkVersion = 36
val androidCompileSdkVersion = 36
val androidBuildToolsVersion = "36.1.0"
val androidCompileNdkVersion by extra(libs.versions.ndk.get())
val androidSourceCompatibility = JavaVersion.VERSION_21
val androidTargetCompatibility = JavaVersion.VERSION_21
val managerVersionCode by extra(getVersionCode())
val managerVersionName by extra(getVersionName())

// Helper for shell cmds.
fun runCommand(vararg command: String): String =
    ProcessBuilder(*command)
        .redirectErrorStream(true)
        .start()
        .inputStream.bufferedReader()
        .use { it.readText().trim() }

// Get cur. branch; strip suffixes (e.g. dev-susfs -> dev)
fun getCurrentBranch(): String {
    val branchRaw = runCommand("git", "rev-parse", "--abbrev-ref", "HEAD")
    return if (branchRaw.isEmpty() || branchRaw == "HEAD") "dev" else branchRaw.split("-")[0]
}

// Get upstream: commit count, hash, describe number
fun getUpstreamDescribe(): Triple<Int, String, Int> {
    val branch = getCurrentBranch()

    // Find common ancestor with origin/$branch, or origin/main as fallback, to exclude local delta
    var baseCommit = runCommand("git", "merge-base", "HEAD", "refs/remotes/origin/$branch")
    if (baseCommit.contains("fatal") || baseCommit.isEmpty()) {
        baseCommit = runCommand("git", "merge-base", "HEAD", "refs/remotes/origin/main")
    }
    if (baseCommit.contains("fatal") || baseCommit.isEmpty()) {
        baseCommit = "HEAD"
    }

    val describe = runCommand("git", "describe", "--tags", "--long", "--abbrev=8", baseCommit)
    val parts = describe.split("-")

    // Fallback if no tags
    if (parts.size < 3) {
        val commitCount = runCommand("git", "rev-list", "--count", baseCommit).let { if (it.isEmpty()) 0 else it.toInt() }
        val hash = runCommand("git", "rev-parse", "--short=8", baseCommit)
        return Triple(commitCount, hash, 0)
    }

    val num = parts[parts.size - 2].toInt()
    val hash = parts.last().removePrefix("g")

    val commitCount = runCommand("git", "rev-list", "--count", baseCommit).let { if (it.isEmpty()) 0 else it.toInt() }

    return Triple(commitCount, hash, num)
}

// Version code calc.
fun getVersionCode(): Int {
    val (commitCount, _, _) = getUpstreamDescribe()
    val major = 1
    return major * 30000 + commitCount
}

// Version name (upstream tag, describe number, hash)
fun getVersionName(): String {
    val (commitCount, hash, num) = getUpstreamDescribe()
    val branch = getCurrentBranch()

    var baseCommit = runCommand("git", "merge-base", "HEAD", "refs/remotes/origin/$branch")
    if (baseCommit.contains("fatal") || baseCommit.isEmpty()) {
        baseCommit = runCommand("git", "merge-base", "HEAD", "refs/remotes/origin/main")
    }
    if (baseCommit.contains("fatal") || baseCommit.isEmpty()) {
        baseCommit = "HEAD"
    }

    val tag = runCommand("git", "describe", "--tags", "--abbrev=0", baseCommit).let { if (it.isEmpty() || it.contains("fatal")) "v0.0.0" else it }
    return "$tag-$num-g$hash"
}

// Root project version info. assignment (extras)
rootProject.extra.set("managerVersionCode", getVersionCode())
rootProject.extra.set("managerVersionName", getVersionName())

subprojects {
    plugins.withType(AndroidBasePlugin::class.java) {
        extensions.configure(CommonExtension::class.java) {
            compileSdk = androidCompileSdkVersion
            ndkVersion = androidCompileNdkVersion

            defaultConfig {
                minSdk = androidMinSdkVersion
                if (this is ApplicationDefaultConfig) {
                    targetSdk = androidTargetSdkVersion
                    versionCode = managerVersionCode
                    versionName = managerVersionName
                }
                ndk {
                    abiFilters += listOf("arm64-v8a", "x86_64", "riscv64")
                }
            }

            lint {
                abortOnError = true
                checkReleaseBuilds = false
            }

            compileOptions {
                sourceCompatibility = androidSourceCompatibility
                targetCompatibility = androidTargetCompatibility
            }
        }
    }
}
