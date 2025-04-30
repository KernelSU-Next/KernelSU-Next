package com.rifsxd.ksunext.ui.webui

import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageInfo
import android.util.Log
import com.dergoogler.mmrl.platform.Platform
import com.dergoogler.mmrl.platform.content.Service
import com.dergoogler.mmrl.platform.model.IProvider
import com.dergoogler.mmrl.platform.model.PlatformIntent
import com.rifsxd.ksunext.IKsuInterface
import com.rifsxd.ksunext.Natives
import com.rifsxd.ksunext.ksuApp
import com.rifsxd.ksunext.ui.KsuService
import com.topjohnwu.superuser.ipc.RootService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import rikka.parcelablelist.ParcelableListSlice

class KsuLibSuProvider : IProvider {
    override val name = "KsuLibSu"

    override fun isAvailable() = true

    override suspend fun isAuthorized() = Natives.becomeManager(ksuApp.packageName)

    private val serviceIntent
        get() = PlatformIntent(
            ksuApp,
            Platform.KsuNext,
            SuService::class.java
        )

    override fun bind(connection: ServiceConnection) {
        RootService.bind(serviceIntent.intent, connection)
    }

    override fun unbind(connection: ServiceConnection) {
        RootService.stop(serviceIntent.intent)
    }
}

suspend fun initPlatform() = withContext(Dispatchers.IO) {
    try {
        val active = Platform.init {
            this.context = ksuApp
            this.platform = Platform.KsuNext
            this.provider = from(KsuLibSuProvider())
        }

        while (!active) {
            delay(1000)
        }

        Platform.mService.addService(
            Service(KsuService::class.java)
        )

        return@withContext active
    } catch (e: Exception) {
        Log.e("KsuLibSu", "Failed to initialize platform", e)
        return@withContext false
    }
}

fun Platform.Companion.getPackages(flags: Int): ParcelableListSlice<PackageInfo> {
    val ksuService: IKsuInterface by lazy {
        IKsuInterface.Stub.asInterface(
            this.mService.getService("ksuService")
        )
    }

    return ksuService.getPackages(flags)
}