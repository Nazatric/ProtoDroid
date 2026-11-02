package com.protodroid.launcher

import kotlinx.coroutines.flow.StateFlow

enum class LauncherPage(val title: String) {
    Home("Home"),
    Setup("File access"),
    Settings("Settings"),
    Drivers("Drivers"),
    Mods("Mods"),
    Layout("Layout"),
    Logs("Logs"),
    About("About"),
}

/** Values match GameSettings.CATEGORY_*. Android remains the source of every option and default. */
enum class SettingsSection(val title: String, val category: Int) {
    Graphics("Graphics", 0),
    Video("Game video", 1),
    Controls("Controls", 2),
    Display("Display", 3),
    Engine("Engine fixes", 4),
}

data class LauncherSetting(
    val key: String,
    val label: String,
    val help: String,
    val options: List<String>,
    val index: Int,
    val category: Int,
) {
    val value: String get() = options.getOrElse(index) { "Unknown" }
}

data class LauncherSettings(
    val preset: Int = 3,
    val rows: List<LauncherSetting> = emptyList(),
) {
    val presetName: String get() = listOf("Low", "Medium", "High", "Custom").getOrElse(preset) { "Custom" }
    fun value(key: String): String = rows.firstOrNull { it.key == key }?.value ?: "Default"
}

data class LauncherMod(
    val id: String,
    val name: String,
    val kind: String,
    val enabled: Boolean,
    val detail: String = "",
    val error: String = "",
    val hasOptions: Boolean = false,
)

data class GameDataStatus(
    val hasFileAccess: Boolean = false,
    val gameDataReady: Boolean = false,
    val status: String = "Waiting for file access",
    val locationLabel: String = "App-managed game location",
    val details: String = "",
    val gamePath: String = "",
    val sharedPath: String = "",
    val appPath: String = "",
    val alternatePath: String = "",
    val tourArmed: Boolean = false,
)

data class LauncherLogDocument(
    val id: String = "native",
    val name: String = "proto-native.log",
    val path: String = "",
    val content: String = "",
    val truncated: Boolean = false,
    val error: String = "",
)

val launcherLogIds = listOf(
    "native" to "Native",
    "prev" to "Previous",
    "dxvk" to "DXVK",
    "tour" to "Tour",
    "journal" to "Journal",
    "result" to "Result",
)

data class LauncherUpdateState(
    val status: String = "hidden",
    val visible: Boolean = false,
    val canPress: Boolean = false,
    val versionName: String = "",
    val progress: Int = -1,
    val downloadedBytes: Long = 0,
    val totalBytes: Long = 0,
    val detail: String = "",
    val requiresUninstall: Boolean = false,
    val warningVersion: String = "",
)

data class LauncherAbout(
    val versionName: String = "Unknown",
    val device: String = "Unknown",
    val os: String = "Unknown",
    val cpu: String = "Unknown",
    val gpu: String = "Unknown",
    val abi: String = "Unknown",
    val ram: String = "Unknown",
    val screen: String = "Unknown",
    val nativeBuild: String = "Unknown",
)

/** Production Android supplies every operation. No preview host is packaged in the application. */
interface LauncherHost {
    val gameData: StateFlow<GameDataStatus>
    val settings: StateFlow<LauncherSettings>
    val mods: StateFlow<List<LauncherMod>>
    val log: StateFlow<LauncherLogDocument>
    val update: StateFlow<LauncherUpdateState>
    val about: StateFlow<LauncherAbout>

    fun motionEnabled(): Boolean
    fun requestFileAccess()
    fun checkGameData()
    fun launchGame()
    fun setSetting(key: String, index: Int)
    fun applyPreset(index: Int)
    fun resetSettings()
    fun openDriverPackages()
    fun openMods()
    fun openTouchLayout()
    fun setModEnabled(id: String, enabled: Boolean)
    fun refreshMods()
    fun selectLog(id: String)
    fun refreshLog()
    fun copyText(text: String)
    fun checkUpdates()
    fun updateNow()
    fun openReleases()
    fun confirmUpdate()
    fun dismissUpdateWarning()
    fun logoTap()
    fun cancelDiagnosticTour()
    fun exitLauncher()
}
