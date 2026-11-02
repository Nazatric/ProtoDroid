package com.protodroid.launcher

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import com.protodroid.launcher.generated.resources.Res
import com.protodroid.launcher.generated.resources.oswald_variable
import com.protodroid.launcher.generated.resources.rubik_dirt
import com.protodroid.launcher.generated.resources.share_tech_mono
import org.jetbrains.compose.resources.Font as ComposeFont

internal val Ink = Color(0xFF080607)
internal val PanelInk = Color(0xE6110B0D)
internal val Blood = Color(0xFFC9141A)
internal val BloodBright = Color(0xFFFF3438)
internal val Bone = Color(0xFFF0EAE4)
internal val Ash = Color(0xFFA49B97)

@Composable
internal fun DirtFamily() = FontFamily(ComposeFont(Res.font.rubik_dirt, FontWeight.Normal))

@Composable
internal fun OswaldFamily() = FontFamily(
    ComposeFont(Res.font.oswald_variable, FontWeight.Light),
    ComposeFont(Res.font.oswald_variable, FontWeight.Normal),
    ComposeFont(Res.font.oswald_variable, FontWeight.Medium),
    ComposeFont(Res.font.oswald_variable, FontWeight.Bold),
)
@Composable
internal fun MonoFamily() = FontFamily(ComposeFont(Res.font.share_tech_mono, FontWeight.Normal))

@Composable
internal fun ProtoTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = BloodBright,
            onPrimary = Bone,
            background = Ink,
            onBackground = Bone,
            surface = PanelInk,
            onSurface = Bone,
            secondary = Ash,
        ),
        content = content,
    )
}