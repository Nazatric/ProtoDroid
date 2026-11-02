package com.protodroid

import android.content.Intent
import android.os.Bundle
import android.view.KeyEvent
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.setContent
import com.protodroid.launcher.LauncherApp
import kotlinx.coroutines.flow.MutableSharedFlow

/** Production launcher activity: Compose presentation, native Android services and SDL game launch. */
class SetupActivity : ComponentActivity() {
    private lateinit var host: ProtoDroidLauncherHost
    private val backEvents = MutableSharedFlow<Unit>(extraBufferCapacity = 1)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ImmersiveMode.apply(this)
        host = ProtoDroidLauncherHost(this)
        host.checkGameData()
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                backEvents.tryEmit(Unit)
            }
        })
        setContent { LauncherApp(host, backEvents) }
    }

    override fun onResume() {
        super.onResume()
        ImmersiveMode.apply(this)
        host.onResume()
    }

    override fun onPause() {
        host.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        host.close()
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) ImmersiveMode.apply(this)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
            if (event.keyCode == KeyEvent.KEYCODE_BUTTON_B) {
                backEvents.tryEmit(Unit)
                return true
            }
            val unicode = event.unicodeChar
            if (unicode > 0 && host.onLetter(unicode.toChar())) return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        host.onRequestPermissionsResult(requestCode, grantResults)
    }

    @Deprecated("Deprecated in Android")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        host.onActivityResult(requestCode, resultCode, data)
    }
}
