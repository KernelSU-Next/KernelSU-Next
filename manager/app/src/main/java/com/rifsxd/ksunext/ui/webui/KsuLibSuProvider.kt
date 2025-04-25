package com.rifsxd.ksunext.ui.webui

import android.content.Context
import android.content.ServiceConnection
import android.util.Log
import com.dergoogler.mmrl.platform.Platform
import com.dergoogler.mmrl.platform.model.IProvider
import com.dergoogler.mmrl.platform.model.PlatformIntent
import com.rifsxd.ksunext.Natives
import com.topjohnwu.superuser.ipc.RootService

class KsuLibSuProvider(
    private val context: Context,
    private val platform: Platform,
) : IProvider {
    override val name = "KsuLibSu"

    override fun isAvailable() = true

    override suspend fun isAuthorized() = Natives.becomeManager(context.packageName)

    private val serviceIntent
        get() = PlatformIntent(
            context,
            platform,
            SuService::class.java
        )

    override fun bind(connection: ServiceConnection) {
        RootService.bind(serviceIntent.intent, connection)
    }

    override fun unbind(connection: ServiceConnection) {
        RootService.stop(serviceIntent.intent)
    }
}

suspend fun Context.initPlatform() = try {
    Platform.init {
        this.context = this@initPlatform
        this.platform = Platform.KsuNext
        this.provider = from(
            KsuLibSuProvider(
                context = this@initPlatform,
                platform = Platform.KsuNext
            )
        )
    }
} catch (e: Exception) {
    Log.e("KsuLibSu", "Failed to initialize platform", e)
    false
}