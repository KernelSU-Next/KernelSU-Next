package com.rifsxd.ksunext.ui.webui

import android.content.Context
import android.content.ServiceConnection
import android.util.Log
import com.dergoogler.mmrl.platform.Platform
import com.dergoogler.mmrl.platform.service.IProvider
import com.dergoogler.mmrl.platform.service.ServiceManagerCompat
import com.dergoogler.mmrl.platform.service.ServiceManagerCompat.getPlatformIntent
import com.rifsxd.ksunext.ui.util.rootAvailable
import com.topjohnwu.superuser.ipc.RootService

class KsuLibSuProvider(
    private val context: Context,
    private val platform: Platform,
) : IProvider {
    override val name = "KsuLibSu"

    override fun isAvailable() = true

    override suspend fun isAuthorized() = rootAvailable()

    override fun bind(connection: ServiceConnection) {
        RootService.bind(getPlatformIntent(context, platform), connection)
    }

    override fun unbind(connection: ServiceConnection) {
        RootService.stop(getPlatformIntent(context, platform))
    }
}

suspend fun Context.initPlatform() = Platform.init {
    context = this@initPlatform
    platform = Platform.KsuNext

    try {
        fromProvider = ServiceManagerCompat.from(
            KsuLibSuProvider(
                context = this@initPlatform,
                platform = Platform.KsuNext
            )
        )
    } catch (e: Exception) {
        Log.e("initPlatform", "Error initializing platform", e)
    }
}