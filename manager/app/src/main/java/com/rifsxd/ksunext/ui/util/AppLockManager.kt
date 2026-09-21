package com.rifsxd.ksunext.ui.util

import android.os.SystemClock

object AppLockManager {
    var activeActivities = 0
    var lastBackgroundTime = 0L
    var isUnlocked = false
    var wasUnlockedBeforeBackground = false

    fun onActivityStart() {
        activeActivities++
    }

    fun onActivityStop(context: android.content.Context) {
        activeActivities--
        if (activeActivities == 0) {
            val prefs = context.getSharedPreferences("settings", android.content.Context.MODE_PRIVATE)
            val timeout = prefs.getLong("app_lock_timeout", 60000L)

            val timeInBackground = SystemClock.elapsedRealtime() - lastBackgroundTime
            val isLogicallyUnlocked = isUnlocked || (wasUnlockedBeforeBackground && timeInBackground < timeout)

            lastBackgroundTime = SystemClock.elapsedRealtime()
            wasUnlockedBeforeBackground = isLogicallyUnlocked
            isUnlocked = false
        }
    }

    fun shouldPrompt(timeout: Long): Boolean {
        if (!isUnlocked) {
            if (lastBackgroundTime == 0L) return true
            if (!wasUnlockedBeforeBackground) return true

            val timeInBackground = SystemClock.elapsedRealtime() - lastBackgroundTime
            if (timeInBackground < timeout) {
                isUnlocked = true
                return false
            }
            return true
        }
        return false
    }

    fun unlock() {
        isUnlocked = true
        wasUnlockedBeforeBackground = true
        lastBackgroundTime = 0L
    }
}
