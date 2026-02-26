package com.rifsxd.ksunext.ui.util.module

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Build
import android.util.Log
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.core.graphics.scale
import androidx.core.net.toUri
import com.topjohnwu.superuser.io.SuFile
import com.topjohnwu.superuser.io.SuFileInputStream
import com.rifsxd.ksunext.R
import com.rifsxd.ksunext.ui.MainActivity
import com.rifsxd.ksunext.ui.util.createRootShell
import com.rifsxd.ksunext.ui.webui.WebUIActivity

object Shortcut {

    fun createModuleActionShortcut(
        context: Context,
        moduleId: String,
        name: String,
        iconUri: String?
    ) {
        val intent = Intent(context, MainActivity::class.java).apply {
            action = Intent.ACTION_VIEW
            putExtra("shortcut_type", "module_action")
            putExtra("module_id", moduleId)
            addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)
        }
        requestPin(context, "module_action_$moduleId", name, iconUri, intent)
    }

    fun createModuleWebUiShortcut(
        context: Context,
        moduleId: String,
        name: String,
        iconUri: String?
    ) {
        val intent = Intent(context, WebUIActivity::class.java).apply {
            action = Intent.ACTION_VIEW
            data = "kernelsu://webui/$moduleId".toUri()
            putExtra("id", moduleId)
            putExtra("name", name)
            putExtra("from_webui_shortcut", true)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        }
        requestPin(context, "module_webui_$moduleId", name, iconUri, intent)
    }

    fun hasModuleActionShortcut(context: Context, moduleId: String) =
        hasPinnedShortcut(context, "module_action_$moduleId")

    fun hasModuleWebUiShortcut(context: Context, moduleId: String) =
        hasPinnedShortcut(context, "module_webui_$moduleId")

    fun deleteModuleActionShortcut(context: Context, moduleId: String) =
        deleteShortcut(context, "module_action_$moduleId")

    fun deleteModuleWebUiShortcut(context: Context, moduleId: String) =
        deleteShortcut(context, "module_webui_$moduleId")

    private fun requestPin(
        context: Context,
        shortcutId: String,
        name: String,
        iconUri: String?,
        shortcutIntent: Intent
    ) {
        if (!ShortcutManagerCompat.isRequestPinShortcutSupported(context)) {
            Toast.makeText(
                context,
                context.getString(R.string.module_shortcut_not_supported),
                Toast.LENGTH_LONG
            ).show()
            return
        }

        val icon = buildIcon(context, iconUri)
            ?: IconCompat.createWithResource(context, R.mipmap.ic_launcher)

        val shortcut = ShortcutInfoCompat.Builder(context, shortcutId)
            .setShortLabel(name)
            .setLongLabel(name)
            .setIcon(icon)
            .setIntent(shortcutIntent)
            .build()

        runCatching { ShortcutManagerCompat.pushDynamicShortcut(context, shortcut) }

        if (hasPinnedShortcut(context, shortcutId)) {
            Toast.makeText(
                context,
                context.getString(R.string.module_shortcut_updated),
                Toast.LENGTH_SHORT
            ).show()
            return
        }

        val callbackAction = "${context.packageName}.SHORTCUT_PINNED.$shortcutId"

        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context, intent: Intent) {
                ctx.unregisterReceiver(this)
                Toast.makeText(
                    ctx,
                    ctx.getString(R.string.module_shortcut_created),
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

        ContextCompat.registerReceiver(
            context,
            receiver,
            IntentFilter(callbackAction),
            ContextCompat.RECEIVER_NOT_EXPORTED
        )

        val pendingIntentFlags =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
            else
                PendingIntent.FLAG_UPDATE_CURRENT

        val callbackPendingIntent = PendingIntent.getBroadcast(
            context,
            shortcutId.hashCode(),
            Intent(callbackAction).setPackage(context.packageName),
            pendingIntentFlags
        )

        val accepted = runCatching {
            ShortcutManagerCompat.requestPinShortcut(
                context,
                shortcut,
                callbackPendingIntent.intentSender
            )
        }.getOrElse { t ->
            false
        }

        if (!accepted) {
            context.unregisterReceiver(receiver)
            Toast.makeText(
                context,
                context.getString(R.string.module_shortcut_not_supported),
                Toast.LENGTH_LONG
            ).show()
        }
    }

    private fun hasPinnedShortcut(context: Context, id: String): Boolean =
        runCatching {
            ShortcutManagerCompat
                .getShortcuts(context, ShortcutManagerCompat.FLAG_MATCH_PINNED)
                .any { it.id == id && it.isEnabled }
        }.getOrDefault(false)

    private fun deleteShortcut(context: Context, id: String) {
        runCatching { ShortcutManagerCompat.removeDynamicShortcuts(context, listOf(id)) }
        runCatching { ShortcutManagerCompat.disableShortcuts(context, listOf(id), "") }
    }

    fun loadShortcutBitmap(context: Context, iconUri: String?): Bitmap? {
        if (iconUri.isNullOrBlank()) return null
        return runCatching {
            val uri = iconUri.toUri()
            val raw = when {
                uri.scheme.equals("su", ignoreCase = true) -> {
                    val path = uri.path?.takeIf { it.isNotBlank() } ?: return null
                    val shell = createRootShell(true)
                    val suFile = SuFile(path).also { it.shell = shell }
                    SuFileInputStream.open(suFile).use { BitmapFactory.decodeStream(it) }
                }
                else -> context.contentResolver.openInputStream(uri)
                    ?.use { BitmapFactory.decodeStream(it) }
            } ?: return null

            val side = minOf(raw.width, raw.height)
            val squared = runCatching {
                Bitmap.createBitmap(raw, (raw.width - side) / 2, (raw.height - side) / 2, side, side)
            }.getOrDefault(raw)
            if (squared !== raw && !raw.isRecycled) raw.recycle()

            if (side > 512) {
                runCatching { squared.scale(512, 512) }
                    .getOrDefault(squared)
                    .also { if (it !== squared && !squared.isRecycled) squared.recycle() }
            } else {
                squared
            }
        }.getOrElse { t ->
            null
        }
    }

    private fun buildIcon(context: Context, iconUri: String?): IconCompat? =
        loadShortcutBitmap(context, iconUri)?.let { IconCompat.createWithBitmap(it) }
}