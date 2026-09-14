package com.rifsxd.ksunext.ui.util

import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.os.Build
import android.os.LocaleList
import androidx.annotation.ChecksSdkIntAtLeast
import androidx.annotation.RequiresApi
import androidx.core.content.edit
import com.rifsxd.ksunext.R
import org.xmlpull.v1.XmlPullParser
import java.util.Locale

object LocaleHelper {

    const val SYSTEM_LANGUAGE_TAG = ""

    private const val PREFS_NAME = "settings"
    private const val PREF_LANGUAGE = "app_locale"
    private const val LEGACY_SYSTEM_LANGUAGE = "system"
    private const val ANDROID_NAMESPACE = "http://schemas.android.com/apk/res/android"

    @get:ChecksSdkIntAtLeast(api = Build.VERSION_CODES.TIRAMISU)
    val usesFrameworkLocaleManager: Boolean
        get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU

    fun getSupportedLocales(context: Context): List<Locale> {
        val parser = context.resources.getXml(R.xml.locales_config)
        return try {
            buildList {
                while (parser.eventType != XmlPullParser.END_DOCUMENT) {
                    if (parser.eventType == XmlPullParser.START_TAG && parser.name == "locale") {
                        val tag = parser.getAttributeValue(ANDROID_NAMESPACE, "name")
                        val locale = tag?.let(Locale::forLanguageTag)
                        if (locale != null && locale.language.isNotEmpty()) {
                            add(locale)
                        }
                    }
                    parser.next()
                }
            }.distinctBy(Locale::toLanguageTag)
        } finally {
            parser.close()
        }
    }

    fun setAppLocale(context: Context, languageTag: String) {
        if (usesFrameworkLocaleManager) {
            setFrameworkLocales(context, LocaleList.forLanguageTags(languageTag))
        } else {
            context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit {
                putString(PREF_LANGUAGE, languageTag.ifEmpty { LEGACY_SYSTEM_LANGUAGE })
            }
        }
    }

    fun applyLanguage(context: Context): Context {
        if (usesFrameworkLocaleManager) return context

        val selected = getStoredLocale(context)?.let { resolveSupportedLocale(context, it) }
            ?: return context
        val locales = buildList {
            add(selected)
            val systemLocales = context.resources.configuration.locales
            for (index in 0 until systemLocales.size()) {
                val locale = systemLocales[index]
                if (none { it.toLanguageTag() == locale.toLanguageTag() }) {
                    add(locale)
                }
            }
        }
        val override = Configuration().apply {
            setLocales(LocaleList(*locales.toTypedArray()))
        }
        return context.createConfigurationContext(override)
    }

    fun getCurrentAppLocale(context: Context): Locale? {
        if (usesFrameworkLocaleManager) {
            val locales = getFrameworkLocales(context)
            val locale = if (locales.isEmpty) null else locales[0]
            return locale?.let { resolveSupportedLocale(context, it) ?: it }
        }

        return getStoredLocale(context)?.let { resolveSupportedLocale(context, it) }
    }

    fun migrateLegacyLocale(context: Context) {
        if (!usesFrameworkLocaleManager) return

        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        if (!prefs.contains(PREF_LANGUAGE)) return

        val locale = getStoredLocale(context)?.let { resolveSupportedLocale(context, it) }
        if (getFrameworkLocales(context).isEmpty && locale != null) {
            setFrameworkLocales(context, LocaleList.forLanguageTags(locale.toLanguageTag()))
        }
        prefs.edit { remove(PREF_LANGUAGE) }
    }

    private fun getStoredLocale(context: Context): Locale? {
        val stored = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getString(PREF_LANGUAGE, LEGACY_SYSTEM_LANGUAGE)
            .orEmpty()
        if (stored.isEmpty() || stored == LEGACY_SYSTEM_LANGUAGE) {
            return null
        }

        val locale = Locale.forLanguageTag(stored.replace('_', '-'))
        return locale.takeIf { it.language.isNotEmpty() }
    }

    private fun resolveSupportedLocale(context: Context, locale: Locale): Locale? {
        val supported = getSupportedLocales(context)
        return supported.firstOrNull {
            it.toLanguageTag().equals(locale.toLanguageTag(), ignoreCase = true)
        } ?: supported.firstOrNull {
            locale.country.isEmpty() && locale.script.isEmpty() && it.language == locale.language
        }
    }

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    private fun getFrameworkLocales(context: Context): LocaleList {
        return context.getSystemService(LocaleManager::class.java).applicationLocales
    }

    @RequiresApi(Build.VERSION_CODES.TIRAMISU)
    private fun setFrameworkLocales(context: Context, locales: LocaleList) {
        context.getSystemService(LocaleManager::class.java).applicationLocales = locales
    }
}
