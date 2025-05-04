package com.rifsxd.ksunext.ui.webui

import android.content.Intent
import android.os.IBinder
import com.dergoogler.mmrl.platform.model.PlatformIntent.Companion.getPlatform
import com.dergoogler.mmrl.platform.service.ServiceManager
import com.topjohnwu.superuser.ipc.RootService

class SuService : RootService() {
    override fun onBind(intent: Intent): IBinder {
        val mode = intent.getPlatform()
        return ServiceManager(mode)
    }
}
