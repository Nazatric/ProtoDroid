package com.protodroid

import android.Manifest
import android.app.Activity
import android.app.ActivityManager
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.SystemClock
import android.provider.Settings
import android.system.ErrnoException
import android.system.Os
import android.widget.Toast
import androidx.activity.ComponentActivity
import com.protodroid.launcher.GameDataStatus
import com.protodroid.launcher.LauncherAbout
import com.protodroid.launcher.LauncherHost
import com.protodroid.launcher.LauncherLogDocument
import com.protodroid.launcher.LauncherMod
import com.protodroid.launcher.LauncherSetting
import com.protodroid.launcher.LauncherSettings
import com.protodroid.launcher.LauncherUpdateState
import java.io.File
import java.util.Locale
import java.util.concurrent.Executors
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** Android-authoritative implementation of the supplied Compose launcher. */
internal class ProtoDroidLauncherHost(
    private val activity: ComponentActivity,
) : LauncherHost {
    private val gameSettings = GameSettings(activity)
    private val driverManager = DriverManager(activity)
    private val worker = Executors.newSingleThreadExecutor()
    private val tourCode = DiagnosticTourCode()
    private val logoTourCode = DiagnosticTourCode()

    private val gameDataState = MutableStateFlow(GameDataStatus())
    private val settingsState = MutableStateFlow(readSettings())
    private val modsState = MutableStateFlow<List<LauncherMod>>(emptyList())
    private val logState = MutableStateFlow(LauncherLogDocument())
    private val updateState = MutableStateFlow(LauncherUpdateState())
    private val aboutState = MutableStateFlow(readAbout())

    override val gameData: StateFlow<GameDataStatus> = gameDataState
    override val settings: StateFlow<LauncherSettings> = settingsState
    override val mods: StateFlow<List<LauncherMod>> = modsState
    override val log: StateFlow<LauncherLogDocument> = logState
    override val update: StateFlow<LauncherUpdateState> = updateState
    override val about: StateFlow<LauncherAbout> = aboutState

    @Volatile private var closed = false
    @Volatile private var selectedLogId = "native"
    @Volatile private var warningVersion = ""
    private var foundGame: File? = null
    private var foundInApp = false
    private var missingUv = false
    private var tourArmed = false

    private val updateManager = UpdateManager(activity, object : UpdateManager.Callback {
        override fun onStateChanged(state: UpdateManager.State) {
            publishUpdate(state)
        }

        override fun onUninstallRequired(versionName: String) {
            warningVersion = versionName
            publishUpdate(updateManager.currentState())
        }
    })

    override fun motionEnabled(): Boolean = LauncherMotion.enabled()

    fun onResume() {
        if (closed) return
        refreshSettings()
        checkGameData()
        refreshMods()
        refreshLog()
        publishUpdate(updateManager.currentState())
        updateManager.onResume()
    }

    fun onPause() = updateManager.onPause()

    fun close() {
        if (closed) return
        closed = true
        driverManager.close()
        updateManager.close()
        worker.shutdownNow()
    }

    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (driverManager.onActivityResult(requestCode, resultCode, data)) return
        updateManager.onActivityResult(requestCode, resultCode)
    }

    fun onRequestPermissionsResult(requestCode: Int, grantResults: IntArray) {
        updateManager.onRequestPermissionsResult(requestCode, grantResults)
        checkGameData()
    }

    override fun requestFileAccess() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                activity.startActivity(Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + activity.packageName),
                ))
            } catch (_: Exception) {
                activity.startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
            }
        } else {
            activity.requestPermissions(arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE), REQUEST_STORAGE)
        }
    }

    override fun checkGameData() {
        val app = appGame()
        app?.mkdirs()
        val shared = sharedGame()
        val sharedSub = File(shared, "game")
        val access = hasSharedAccess()
        if (access) shared.mkdirs()
        val candidates = if (access) arrayOf(app, shared, sharedSub) else arrayOf(app)
        foundGame = GameFiles.firstComplete(*candidates)
        foundInApp = foundGame != null && foundGame == app
        missingUv = foundGame == null && GameFiles.anyMissingUv(*candidates)
        val found = foundGame
        val status = when {
            missingUv -> "Missing proto_uv.dat; copy it from your PC game"
            tourArmed && found != null -> "Tour armed: automatic tests, isolated saves, then the app closes"
            tourArmed -> "Tour armed; complete game files are required"
            found != null -> "Ready to play"
            access -> "Game files not found. Copy your own PC files, then check again"
            else -> "Waiting for file access"
        }
        val appPath = app?.absolutePath
            ?: "/storage/emulated/0/Android/data/${activity.packageName}/files/game"
        gameDataState.value = GameDataStatus(
            hasFileAccess = access,
            gameDataReady = found != null,
            status = status,
            locationLabel = found?.absolutePath ?: appPath,
            details = when {
                missingUv -> "prototypeenginef.dll was found, but proto_uv.dat is incomplete or missing. No UV data is fabricated."
                access -> "Shared saves remain in /storage/emulated/0/Prototype"
                else -> "App-specific game data and saves can be used without shared-storage access"
            },
            gamePath = found?.absolutePath.orEmpty(),
            sharedPath = shared.absolutePath,
            appPath = appPath,
            alternatePath = sharedSub.absolutePath,
            tourArmed = tourArmed,
        )
    }

    override fun launchGame() {
        checkGameData()
        val found = foundGame ?: return
        launch(if (foundInApp) null else found, tourArmed)
    }

    private fun launch(gameDir: File?, tour: Boolean): Boolean {
        try {
            if (tour) Os.setenv("PROTODROID_DIAGNOSTIC_TOUR", "1", true)
            else Os.unsetenv("PROTODROID_DIAGNOSTIC_TOUR")
        } catch (_: ErrnoException) {
            Toast.makeText(activity, if (tour) {
                "Could not arm the diagnostic tour; normal Play was not substituted."
            } else {
                "Could not clear diagnostic-tour mode; Play was cancelled to protect your saves."
            }, Toast.LENGTH_LONG).show()
            return false
        }
        try {
            if (gameDir == null) Os.unsetenv("PROTODROID_GAME")
            else Os.setenv("PROTODROID_GAME", gameDir.absolutePath, true)
        } catch (_: ErrnoException) {
            // Native launch retains its app-folder fallback.
        }
        val driver = driverManager.driverForLaunch()
        val selected = driverManager.selected()
        try {
            if (driver == null) {
                Os.unsetenv("PROTODROID_CUSTOM_DRIVER")
                Os.unsetenv("PROTODROID_CUSTOM_DRIVER_BACKEND")
            } else {
                Os.setenv("PROTODROID_CUSTOM_DRIVER", driver.absolutePath, true)
                Os.setenv(
                    "PROTODROID_CUSTOM_DRIVER_BACKEND",
                    selected?.installed?.backend ?: DriverCatalog.TURNIP,
                    true,
                )
            }
        } catch (_: ErrnoException) {
            // Bundled automatic selection remains available.
        }
        activity.startActivity(Intent(activity, ProtoActivity::class.java))
        activity.finish()
        return true
    }

    override fun setSetting(key: String, index: Int) {
        val settingIndex = gameSettings.settings.indexOfFirst { it.key == key }
        if (settingIndex < 0) return
        val setting = gameSettings.settings[settingIndex]
        if (key == "touch_layout") {
            openTouchLayout()
            return
        }
        if (index !in setting.names.indices) return
        gameSettings.setIndex(settingIndex, index)
        refreshSettings()
    }

    override fun applyPreset(index: Int) {
        gameSettings.applyPreset(index)
        refreshSettings()
    }

    override fun resetSettings() {
        gameSettings.resetToDefaults()
        refreshSettings()
    }

    private fun refreshSettings() {
        settingsState.value = readSettings()
    }

    private fun readSettings(): LauncherSettings = LauncherSettings(
        preset = gameSettings.graphicsPreset(),
        rows = gameSettings.settings.map { setting ->
            LauncherSetting(
                key = setting.key,
                label = setting.label,
                help = GameSettings.helpFor(setting),
                options = setting.names.toList(),
                index = setting.index,
                category = GameSettings.categoryOf(setting),
            )
        },
    )

    override fun openDriverPackages() {
        activity.startActivity(Intent(activity, DriverActivity::class.java))
        LauncherMotion.transition(activity)
    }

    override fun openMods() {
        activity.startActivity(Intent(activity, ModsActivity::class.java))
        LauncherMotion.transition(activity)
    }

    override fun openTouchLayout() {
        activity.startActivity(Intent(activity, TouchLayoutActivity::class.java))
        LauncherMotion.transition(activity)
    }

    override fun refreshMods() {
        val models = ModManager.list(activity).map { mod ->
            val kinds = buildList {
                if (mod.files.isNotBlank()) add("Game data")
                if (mod.textures.isNotBlank()) add("Textures")
                if (mod.plugin.isNotBlank()) add("Native plugin")
                if (mod.script.isNotBlank()) add("Script")
            }
            LauncherMod(
                id = mod.id,
                name = mod.name,
                kind = if (kinds.isEmpty()) "Local mod" else kinds.joinToString(" · "),
                enabled = mod.enabled,
                detail = listOf(mod.description, mod.launchError).filter { it.isNotBlank() }.joinToString("\n"),
                error = mod.error,
                hasOptions = mod.options.isNotEmpty(),
            )
        }
        modsState.value = models
    }

    override fun setModEnabled(id: String, enabled: Boolean) {
        worker.execute {
            var message: String
            try {
                val mod = ModManager.list(activity).firstOrNull { it.id == id }
                    ?: throw IllegalStateException("The mod is no longer installed")
                ModManager.setEnabled(activity, mod, enabled)
                message = (if (enabled) "Enabled '" else "Disabled '") + mod.name + "' for the next Play"
            } catch (error: Exception) {
                message = "Could not change the mod: " + safeMessage(error)
            }
            activity.runOnUiThread {
                if (closed) return@runOnUiThread
                refreshMods()
                Toast.makeText(activity, message, Toast.LENGTH_LONG).show()
            }
        }
    }

    override fun selectLog(id: String) {
        selectedLogId = id
        refreshLog()
    }

    override fun refreshLog() {
        val id = selectedLogId
        worker.execute {
            val entry = LauncherLog.read(activity.getExternalFilesDir(null), sharedGame(), id)
            if (id != selectedLogId || closed) return@execute
            logState.value = LauncherLogDocument(
                id = id,
                name = entry.name,
                path = entry.path,
                content = entry.content,
                truncated = entry.truncated,
                error = entry.error,
            )
        }
    }

    override fun copyText(text: String) {
        val clipboard = activity.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
        clipboard?.setPrimaryClip(ClipData.newPlainText("ProtoDroid log", text))
        Toast.makeText(activity, "Log tail copied", Toast.LENGTH_SHORT).show()
    }

    override fun checkUpdates() = updateManager.onCheckUpdatesPressed()
    override fun updateNow() = updateManager.onUpdatePressed()
    override fun openReleases() = updateManager.onReleasesPressed()

    override fun confirmUpdate() {
        warningVersion = ""
        publishUpdate(updateManager.currentState())
        updateManager.confirmUninstall()
    }

    override fun dismissUpdateWarning() {
        warningVersion = ""
        publishUpdate(updateManager.currentState())
    }

    private fun publishUpdate(state: UpdateManager.State) {
        updateState.value = LauncherUpdateState(
            status = when (state.status) {
                UpdateManager.State.CHECKING -> "checking"
                UpdateManager.State.AVAILABLE -> "available"
                UpdateManager.State.DOWNLOADING -> "downloading"
                UpdateManager.State.VERIFYING -> "verifying"
                UpdateManager.State.READY -> "ready"
                UpdateManager.State.ERROR -> "error"
                else -> "hidden"
            },
            visible = state.visible(),
            canPress = state.canPress(),
            versionName = state.versionName,
            progress = state.progress,
            downloadedBytes = state.downloadedBytes,
            totalBytes = state.totalBytes,
            detail = state.detail,
            requiresUninstall = state.requiresUninstall,
            warningVersion = warningVersion,
        )
    }

    override fun logoTap() {
        if (logoTourCode.onLogoTap(SystemClock.uptimeMillis())) toggleDiagnosticTour()
    }

    fun onLetter(letter: Char): Boolean {
        if (!tourCode.onLetter(letter)) return false
        toggleDiagnosticTour()
        return true
    }

    override fun cancelDiagnosticTour() {
        if (tourArmed) toggleDiagnosticTour()
    }

    private fun toggleDiagnosticTour() {
        tourArmed = !tourArmed
        checkGameData()
    }

    override fun exitLauncher() = activity.finish()

    private fun appGame(): File? = activity.getExternalFilesDir(null)?.let { File(it, "game") }

    private fun hasSharedAccess(): Boolean = if (Build.VERSION.SDK_INT >= 30) {
        Environment.isExternalStorageManager()
    } else {
        activity.checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
    }

    private fun readAbout(): LauncherAbout {
        val mali = File("/dev/mali0").exists() || File("/dev/mali").exists() || File("/dev/mali_kbase").exists()
        val adreno = !mali && File("/dev/kgsl-3d0").exists()
        val version = try {
            activity.packageManager.getPackageInfo(activity.packageName, 0).versionName ?: "Unknown"
        } catch (_: Exception) {
            "Unknown"
        }
        return LauncherAbout(
            versionName = version,
            device = "${Build.MANUFACTURER} ${Build.MODEL}",
            os = "Android ${Build.VERSION.RELEASE} · API ${Build.VERSION.SDK_INT}",
            cpu = Build.HARDWARE?.ifBlank { "Unknown" } ?: "Unknown",
            gpu = DriverScreenPolicy.deviceFamily(mali, adreno),
            abi = Build.SUPPORTED_ABIS.joinToString(", "),
            ram = totalMemory(),
            screen = with(activity.resources.displayMetrics) { "${widthPixels}×${heightPixels}" },
            nativeBuild = Build.FINGERPRINT ?: "Unknown",
        )
    }

    private fun totalMemory(): String {
        try {
            val manager = activity.getSystemService(Activity.ACTIVITY_SERVICE) as? ActivityManager
            val info = ActivityManager.MemoryInfo()
            if (manager != null) {
                manager.getMemoryInfo(info)
                return String.format(Locale.ROOT, "%.1f GiB", info.totalMem / (1024.0 * 1024.0 * 1024.0))
            }
        } catch (_: RuntimeException) {
        }
        return "Unknown"
    }

    private companion object {
        const val REQUEST_STORAGE = 1

        fun sharedGame() = File(Environment.getExternalStorageDirectory(), "Prototype")

        fun safeMessage(error: Exception): String {
            val message = error.message
            return if (message.isNullOrBlank()) error.javaClass.simpleName else message
        }
    }
}
