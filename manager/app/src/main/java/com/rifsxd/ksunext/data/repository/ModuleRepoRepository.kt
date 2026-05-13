package com.rifsxd.ksunext.data.repository

import com.rifsxd.ksunext.data.model.RepoModule

interface ModuleRepoRepository {
    suspend fun fetchModules(): Result<List<RepoModule>>
}
