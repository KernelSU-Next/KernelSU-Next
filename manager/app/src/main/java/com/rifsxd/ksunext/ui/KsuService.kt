package com.rifsxd.ksunext.ui

import android.app.ActivityThread
import android.content.Context
import android.content.ContextWrapper
import android.content.pm.PackageInfo
import android.os.IBinder
import android.os.UserManager
import android.util.Log
import com.dergoogler.mmrl.platform.content.IService
import com.dergoogler.mmrl.platform.stub.IServiceManager
import com.rifsxd.ksunext.IKsuInterface
import com.topjohnwu.superuser.ipc.RootService.USER_SERVICE
import rikka.parcelablelist.ParcelableListSlice

class KsuService: IService {
    override val name: String
        get() = "ksuService"

    override fun create(manager: IServiceManager): IBinder = KsuServiceImpl()
}

class KsuServiceImpl : IKsuInterface.Stub() {
    val context: Context
        get() {
            var context: Context = ActivityThread.currentApplication()
            while (context is ContextWrapper) {
                context = context.baseContext
            }

            return context
        }


    override fun getPackages(flags: Int): ParcelableListSlice<PackageInfo> {
        val list: List<PackageInfo> = getInstalledPackagesAll(flags)
        Log.i(TAG, "getPackages: " + list.size)
        return ParcelableListSlice(list)
    }

    private val userIds: List<Int>
        get() {
            val result: MutableList<Int> =
                ArrayList()
            val um =
                context.getSystemService(USER_SERVICE) as UserManager
            val userProfiles = um.userProfiles
            for (userProfile in userProfiles) {
                val userId = userProfile.hashCode()
                result.add(userProfile.hashCode())
            }
            return result
        }

    private fun getInstalledPackagesAll(flags: Int): ArrayList<PackageInfo> {
        val packages = ArrayList<PackageInfo>()
        for (userId in userIds) {
            Log.i(TAG, "getInstalledPackagesAll: $userId")
            packages.addAll(getInstalledPackagesAsUser(flags, userId))
        }
        return packages
    }

    private fun getInstalledPackagesAsUser(flags: Int, userId: Int): List<PackageInfo> {
        try {
            val pm = context.packageManager
            val getInstalledPackagesAsUser = pm.javaClass.getDeclaredMethod(
                "getInstalledPackagesAsUser",
                Int::class.javaPrimitiveType,
                Int::class.javaPrimitiveType
            )

            return getInstalledPackagesAsUser.invoke(pm, flags, userId) as List<PackageInfo>
        } catch (e: Throwable) {
            Log.e(TAG, "err", e)
        }

        return ArrayList()
    }

    private companion object Default {
        const val TAG = "KsuService"
    }
}
