package com.rifsxd.ksunext.data.repository

import com.rifsxd.ksunext.data.model.Module
import com.rifsxd.ksunext.data.model.ModuleUpdateInfo

interface ModuleRepository {
    suspend fun getModules(): Result<List<Module>>
    suspend fun checkUpdate(module: Module): Result<ModuleUpdateInfo>
}
