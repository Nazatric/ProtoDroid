package com.protodroid.launcher

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.protodroid.launcher.generated.resources.Res
import com.protodroid.launcher.generated.resources.bink_01
import com.protodroid.launcher.generated.resources.bink_02
import com.protodroid.launcher.generated.resources.bink_03
import com.protodroid.launcher.generated.resources.bink_04
import com.protodroid.launcher.generated.resources.bink_05
import com.protodroid.launcher.generated.resources.bink_06
import com.protodroid.launcher.generated.resources.bink_07
import com.protodroid.launcher.generated.resources.bink_08
import com.protodroid.launcher.generated.resources.bink_09
import com.protodroid.launcher.generated.resources.bink_10
import com.protodroid.launcher.generated.resources.intro_03
import com.protodroid.launcher.generated.resources.intro_06
import com.protodroid.launcher.generated.resources.intro_09
import com.protodroid.launcher.generated.resources.intro_11
import com.protodroid.launcher.generated.resources.intro_12
import com.protodroid.launcher.generated.resources.intro_13
import com.protodroid.launcher.generated.resources.launcher_icon
import com.protodroid.launcher.generated.resources.wall_ashen_profile
import com.protodroid.launcher.generated.resources.wall_biomass_title
import com.protodroid.launcher.generated.resources.wall_blacklight_portrait
import com.protodroid.launcher.generated.resources.wall_claw_white
import com.protodroid.launcher.generated.resources.wall_manhattan_smoke
import com.protodroid.launcher.generated.resources.wall_mercer_city
import com.protodroid.launcher.generated.resources.wall_mercer_cityline
import com.protodroid.launcher.generated.resources.wall_mercer_fog
import com.protodroid.launcher.generated.resources.wall_mercer_silhouette
import com.protodroid.launcher.generated.resources.wall_mercer_urban
import com.protodroid.launcher.generated.resources.wall_quarantine_city
import com.protodroid.launcher.generated.resources.wall_sunset_helicopters
import com.protodroid.launcher.generated.resources.wall_viral_title
import com.protodroid.launcher.generated.resources.wall_white_biomass
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.DrawableResource
import org.jetbrains.compose.resources.painterResource

private val homeScenes = listOf(
    Res.drawable.wall_mercer_city,
    Res.drawable.wall_blacklight_portrait,
    Res.drawable.wall_biomass_title,
    Res.drawable.wall_sunset_helicopters,
    Res.drawable.wall_ashen_profile,
    Res.drawable.wall_viral_title,
    Res.drawable.wall_claw_white,
    Res.drawable.wall_mercer_silhouette,
    Res.drawable.wall_mercer_fog,
    Res.drawable.wall_mercer_cityline,
    Res.drawable.wall_white_biomass,
    Res.drawable.wall_quarantine_city,
    Res.drawable.wall_manhattan_smoke,
)

private val binkScenes = listOf(
    Res.drawable.bink_01,
    Res.drawable.bink_02,
    Res.drawable.bink_03,
    Res.drawable.bink_04,
    Res.drawable.bink_05,
    Res.drawable.bink_06,
    Res.drawable.bink_07,
    Res.drawable.bink_08,
    Res.drawable.bink_09,
    Res.drawable.bink_10,
)

private val introScenes = listOf(
    Res.drawable.intro_03,
    Res.drawable.intro_06,
    Res.drawable.intro_09,
    Res.drawable.intro_11,
    Res.drawable.intro_12,
    Res.drawable.intro_13,
)

private val LocalLauncherMotion = staticCompositionLocalOf { true }

@Composable
fun LauncherApp(host: LauncherHost, backEvents: Flow<Unit>? = null) {
    val gameData by host.gameData.collectAsState()
    val settings by host.settings.collectAsState()
    val mods by host.mods.collectAsState()
    val log by host.log.collectAsState()
    val update by host.update.collectAsState()
    val about by host.about.collectAsState()
    val motion = remember(host) { host.motionEnabled() }
    var page by remember(host) {
        mutableStateOf(if (gameData.hasFileAccess || gameData.gameDataReady) LauncherPage.Home else LauncherPage.Setup)
    }
    val currentPage by rememberUpdatedState(page)
    val currentUpdate by rememberUpdatedState(update)

    LaunchedEffect(backEvents) {
        backEvents?.collect {
            when {
                currentUpdate.warningVersion.isNotEmpty() -> host.dismissUpdateWarning()
                currentPage == LauncherPage.Home -> host.exitLauncher()
                else -> page = LauncherPage.Home
            }
        }
    }

    CompositionLocalProvider(LocalLauncherMotion provides motion) {
        ProtoTheme {
            BoxWithConstraints(Modifier.fillMaxSize().background(Ink)) {
                val compact = maxWidth < 680.dp || maxHeight < 370.dp
                AnimatedContent(
                    targetState = page,
                    transitionSpec = {
                        val enterDuration = if (motion) 300 else 0
                        val exitDuration = if (motion) 180 else 0
                        (fadeIn(tween(enterDuration)) + scaleIn(
                            initialScale = if (motion) 0.995f else 1f,
                            animationSpec = tween(enterDuration),
                        )) togetherWith fadeOut(tween(exitDuration)) using SizeTransform(clip = false)
                    },
                    label = "launcher page",
                ) { animatedPage ->
                    when (animatedPage) {
                        LauncherPage.Home -> HomeScreen(
                            compact = compact,
                            settings = settings,
                            gameData = gameData,
                            update = update,
                            onPage = { page = it },
                            onPlay = {
                                if (gameData.gameDataReady) host.launchGame() else page = LauncherPage.Setup
                            },
                            onLogoTap = host::logoTap,
                            onExit = host::exitLauncher,
                        )
                        LauncherPage.Setup -> SetupScreen(
                            host = host,
                            data = gameData,
                            onBack = { page = LauncherPage.Home },
                            onContinue = { page = LauncherPage.Home },
                        )
                        LauncherPage.Settings -> SettingsScreen(
                            settings = settings,
                            onBack = { page = LauncherPage.Home },
                            onPage = { page = it },
                            host = host,
                        )
                        LauncherPage.Drivers -> DriversScreen(
                            settings = settings,
                            host = host,
                            onBack = { page = LauncherPage.Home },
                        )
                        LauncherPage.Mods -> ModsScreen(
                            mods = mods,
                            host = host,
                            onBack = { page = LauncherPage.Home },
                        )
                        LauncherPage.Layout -> LayoutScreen(
                            host = host,
                            onBack = { page = LauncherPage.Home },
                        )
                        LauncherPage.Logs -> LogsScreen(
                            document = log,
                            host = host,
                            onBack = { page = LauncherPage.Home },
                        )
                        LauncherPage.About -> AboutScreen(
                            about = about,
                            update = update,
                            host = host,
                            onBack = { page = LauncherPage.Home },
                        )
                    }
                }
                if (update.warningVersion.isNotEmpty()) {
                    UpdateWarning(update.warningVersion, host)
                }
            }
        }
    }
}

@Composable
private fun CinematicBackdrop(
    scenes: List<DrawableResource>,
    modifier: Modifier = Modifier,
    darken: Float = 0.38f,
) {
    val motion = LocalLauncherMotion.current
    var layers by remember(scenes) { mutableStateOf(listOf(0)) }
    LaunchedEffect(scenes, motion) {
        if (motion && scenes.size > 1) {
            while (true) {
                delay(10_000)
                layers = listOf(layers.last(), (layers.last() + 1) % scenes.size)
            }
        }
    }

    Box(modifier.fillMaxSize()) {
        // Outgoing scene stays mounted at full opacity with its finished zoom;
        // the incoming scene fades over it. Pool aspects are uniform, so the
        // cover-crop scale is identical on both layers and the cut cannot jump.
        layers.forEach { index ->
            key(index) { SceneImage(scenes[index], entering = motion && scenes.size > 1) }
        }
        if (motion && scenes.size > 1) {
            // Decode the next scene invisibly before its turn.
            Image(
                painter = painterResource(scenes[(layers.last() + 1) % scenes.size]),
                contentDescription = null,
                modifier = Modifier.fillMaxSize().graphicsLayer { alpha = 0f },
            )
        }
        Box(
            Modifier.fillMaxSize().background(
                Brush.horizontalGradient(listOf(Ink.copy(alpha = 0.72f), Color.Transparent, Ink.copy(alpha = 0.15f)))
            )
        )
        Box(
            Modifier.fillMaxSize().background(
                Brush.verticalGradient(listOf(Ink.copy(alpha = 0.48f), Color.Transparent, Ink.copy(alpha = 0.86f)))
            )
        )
        Box(Modifier.fillMaxSize().background(Ink.copy(alpha = darken.coerceIn(0f, 0.6f))))
    }
}

@Composable
private fun SceneImage(res: DrawableResource, entering: Boolean) {
    val motion = LocalLauncherMotion.current
    val fade = remember { Animatable(if (entering && motion) 0f else 1f) }
    val zoom = remember { Animatable(1.06f) }
    LaunchedEffect(Unit) {
        if (motion) {
            if (entering) launch { fade.animateTo(1f, tween(1_600, easing = FastOutSlowInEasing)) }
            zoom.animateTo(1.13f, tween(11_600, easing = LinearEasing))
        }
    }
    Image(
        painter = painterResource(res),
        contentDescription = null,
        contentScale = ContentScale.Crop,
        modifier = Modifier.fillMaxSize().graphicsLayer {
            alpha = fade.value
            scaleX = zoom.value
            scaleY = zoom.value
            translationX = -(zoom.value - 1.06f) * size.width * 0.14f
            translationY = -(zoom.value - 1.06f) * size.height * 0.08f
        },
    )
}

@Composable
private fun HomeScreen(
    compact: Boolean,
    settings: LauncherSettings,
    gameData: GameDataStatus,
    update: LauncherUpdateState,
    onPage: (LauncherPage) -> Unit,
    onPlay: () -> Unit,
    onLogoTap: () -> Unit,
    onExit: () -> Unit,
) {
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val showStatusPanel = maxWidth >= 650.dp
        val viewportHeight = maxHeight
        CinematicBackdrop(homeScenes, darken = 0.22f)
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier.fillMaxWidth().heightIn(min = 42.dp).padding(horizontal = 16.dp, vertical = 5.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
            ) {
                Image(
                    painterResource(Res.drawable.launcher_icon),
                    "ProtoDroid diagnostic tour access",
                    Modifier.size(28.dp).clip(RoundedCornerShape(6.dp)).clickable(onClick = onLogoTap),
                )
                Text("ProtoDroid", fontFamily = OswaldFamily(), fontSize = 13.sp, letterSpacing = 2.sp, color = Bone)
                Spacer(Modifier.weight(1f))
                if (!gameData.hasFileAccess) StatusTag("ACCESS NEEDED", BloodBright)
                else if (gameData.gameDataReady) StatusTag("READY", Bone)
                else StatusTag("NO GAME DATA", BloodBright)
            }

            Row(
                Modifier.fillMaxWidth().weight(1f).padding(horizontal = if (compact) 18.dp else 30.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(20.dp),
            ) {
                Box(Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState())) {
                Column(
                    Modifier.fillMaxWidth().heightIn(min = viewportHeight).padding(vertical = 6.dp),
                    verticalArrangement = Arrangement.Center,
                ) {
                    Text(
                        "[PROTOTYPE]",
                        fontFamily = DirtFamily(),
                        fontSize = if (compact) 31.sp else 42.sp,
                        lineHeight = if (compact) 38.sp else 50.sp,
                        color = Bone,
                        maxLines = 1,
                        softWrap = false,
                    )
                    Text(
                        "NATIVE ANDROID PORT",
                        Modifier.padding(start = 5.dp, bottom = if (compact) 10.dp else 16.dp),
                        fontFamily = OswaldFamily(),
                        fontSize = if (compact) 9.sp else 11.sp,
                        letterSpacing = 3.sp,
                        color = Ash,
                        maxLines = 1,
                    )

                    val menuHeight = if (compact) 31.dp else 39.dp
                    MainMenuItem("Play", emphasized = true, height = menuHeight, onClick = onPlay)
                    MainMenuItem("Settings", height = menuHeight) { onPage(LauncherPage.Settings) }
                    MainMenuItem("Drivers", height = menuHeight) { onPage(LauncherPage.Drivers) }
                    MainMenuItem("Mods", height = menuHeight) { onPage(LauncherPage.Mods) }
                    MainMenuItem("Layout", height = menuHeight) { onPage(LauncherPage.Layout) }
                    MainMenuItem("Logs", height = menuHeight) { onPage(LauncherPage.Logs) }
                    MainMenuItem("About", height = menuHeight) { onPage(LauncherPage.About) }
                }
                }

                if (showStatusPanel) {
                    GameStatusPanel(
                        gameData = gameData,
                        settings = settings,
                        onAccess = { onPage(LauncherPage.Setup) },
                        onSettings = { onPage(LauncherPage.Settings) },
                        modifier = Modifier.widthIn(min = 200.dp, max = 260.dp).fillMaxWidth(0.34f),
                    )
                }
            }

            Row(
                Modifier.fillMaxWidth().heightIn(min = 28.dp).background(Color.Black.copy(alpha = 0.48f)).padding(horizontal = 14.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    when {
                        gameData.tourArmed -> "DIAGNOSTIC TOUR ARMED · ISOLATED SAVES"
                        gameData.gameDataReady -> "READY TO PLAY"
                        else -> "ADD YOUR OWN GAME FILES"
                    },
                    Modifier.weight(1f),
                    fontFamily = MonoFamily(),
                    fontSize = 10.sp,
                    color = Bone.copy(alpha = 0.78f),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (update.visible) {
                    TextButton(onClick = { onPage(LauncherPage.About) }, contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp)) {
                        Text("UPDATE", fontFamily = OswaldFamily(), fontSize = 10.sp, letterSpacing = 1.4.sp, color = BloodBright)
                    }
                }
                TextButton(onClick = onExit, contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp)) {
                    Text("EXIT", fontFamily = OswaldFamily(), fontSize = 10.sp, letterSpacing = 1.6.sp, color = Ash)
                }
            }
        }
    }
}

@Composable
private fun GameStatusPanel(
    gameData: GameDataStatus,
    settings: LauncherSettings,
    onAccess: () -> Unit,
    onSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier,
        color = PanelInk.copy(alpha = 0.9f),
        shape = RoundedCornerShape(3.dp),
        border = BorderStroke(1.dp, Color.White.copy(alpha = 0.12f)),
    ) {
        Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(7.dp).background(if (gameData.gameDataReady) Bone else BloodBright, RoundedCornerShape(50)))
                Text("GAME DATA", Modifier.padding(start = 7.dp), fontFamily = OswaldFamily(), fontSize = 11.sp, letterSpacing = 2.sp, color = Ash)
                Spacer(Modifier.weight(1f))
                Text(if (gameData.gameDataReady) "READY" else "MISSING", fontFamily = OswaldFamily(), fontSize = 11.sp, letterSpacing = 1.sp, color = if (gameData.gameDataReady) Bone else BloodBright)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(settings.value("vulkan_driver"), fontFamily = MonoFamily(), fontSize = 10.sp, color = Bone.copy(alpha = 0.86f), maxLines = 1)
                Text("·", color = Ash)
                Text(settings.value("render_height"), fontFamily = MonoFamily(), fontSize = 10.sp, color = Bone.copy(alpha = 0.86f))
                Text("·", color = Ash)
                Text("FPS ${settings.value("fps_limit")}", fontFamily = MonoFamily(), fontSize = 10.sp, color = Bone.copy(alpha = 0.86f))
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                ActionButton(if (gameData.hasFileAccess) "CHECK" else "ALLOW ACCESS", Modifier.weight(1f), onAccess)
                OutlineButton("SETTINGS", onSettings)
            }
        }
    }
}

@Composable
private fun MainMenuItem(label: String, emphasized: Boolean = false, height: Dp, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(height).clip(RoundedCornerShape(2.dp)).clickable(onClick = onClick).padding(horizontal = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("›", fontFamily = OswaldFamily(), fontSize = if (emphasized) 24.sp else 19.sp, color = BloodBright, modifier = Modifier.width(22.dp))
        Text(
            label.uppercase(),
            fontFamily = OswaldFamily(),
            fontWeight = if (emphasized) FontWeight.Bold else FontWeight.Medium,
            fontSize = if (emphasized) 24.sp else 18.sp,
            letterSpacing = 2.sp,
            color = Bone.copy(alpha = if (emphasized) 1f else 0.88f),
            maxLines = 1,
            softWrap = false,
        )
    }
}

@Composable
private fun SetupScreen(host: LauncherHost, data: GameDataStatus, onBack: () -> Unit, onContinue: () -> Unit) {
    PageFrame("FILE ACCESS", listOf(Res.drawable.wall_sunset_helicopters, Res.drawable.wall_quarantine_city), onBack) {
        Box(Modifier.fillMaxSize().padding(18.dp), contentAlignment = Alignment.Center) {
            Surface(
                Modifier.fillMaxWidth().widthIn(max = 640.dp),
                color = PanelInk.copy(alpha = 0.92f),
                shape = RoundedCornerShape(3.dp),
                border = BorderStroke(1.dp, Color.White.copy(alpha = 0.12f)),
            ) {
                Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatusTag(
                        when {
                            data.gameDataReady -> "GAME DATA READY"
                            data.hasFileAccess -> "ACCESS ALLOWED"
                            else -> "PERMISSION NEEDED"
                        },
                        if (data.gameDataReady) Bone else BloodBright,
                    )
                    Text(
                        if (data.hasFileAccess) "GAME FILE ACCESS" else "ALLOW FILE ACCESS",
                        fontFamily = DirtFamily(),
                        fontSize = 27.sp,
                        lineHeight = 32.sp,
                        color = Bone,
                    )
                    Text(
                        data.status,
                        fontFamily = OswaldFamily(),
                        fontSize = 14.sp,
                        lineHeight = 19.sp,
                        color = Bone.copy(alpha = 0.78f),
                    )
                    Text(
                        "Use the app-specific game folder or the shared Prototype folder. Files are detected in place; Play never substitutes a diagnostic tour for a normal launch.",
                        fontFamily = OswaldFamily(),
                        fontSize = 12.sp,
                        lineHeight = 16.sp,
                        color = Ash,
                    )
                    if (data.details.isNotBlank()) {
                        Text(data.details, fontFamily = MonoFamily(), fontSize = 11.sp, color = Ash)
                    }
                    if (data.gamePath.isNotBlank()) {
                        Text("FOUND · ${data.gamePath}", fontFamily = MonoFamily(), fontSize = 10.sp, color = Bone)
                    } else {
                        Text(data.appPath, fontFamily = MonoFamily(), fontSize = 9.sp, color = Ash)
                        Text(data.sharedPath, fontFamily = MonoFamily(), fontSize = 9.sp, color = Ash)
                    }
                    Row(
                        Modifier.horizontalScroll(rememberScrollState()),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        if (!data.hasFileAccess && !data.gameDataReady) {
                            ActionButton("ALLOW FILE ACCESS", onClick = host::requestFileAccess)
                        } else {
                            ActionButton("CONTINUE", onClick = onContinue)
                            if (!data.gameDataReady) OutlineButton("CHECK AGAIN", onClick = host::checkGameData)
                        }
                        if (data.tourArmed) OutlineButton("CANCEL TOUR", onClick = host::cancelDiagnosticTour)
                    }
                }
            }
        }
    }
}

@Composable
private fun SettingsScreen(
    settings: LauncherSettings,
    onBack: () -> Unit,
    onPage: (LauncherPage) -> Unit,
    host: LauncherHost,
) {
    var section by remember { mutableStateOf(SettingsSection.Graphics) }
    PageFrame("SETTINGS", listOf(Res.drawable.wall_ashen_profile, Res.drawable.wall_manhattan_smoke), onBack) {
        BoxWithConstraints(Modifier.fillMaxSize()) {
            val compact = maxWidth < 660.dp
            if (compact) {
                Column(Modifier.fillMaxSize()) {
                    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 5.dp)) {
                        SettingsSection.entries.forEach { item -> SectionButton(item, section == item) { section = item } }
                    }
                    SettingsRows(section, settings, onPage, host, Modifier.weight(1f).fillMaxWidth())
                }
            } else {
                Row(Modifier.fillMaxSize()) {
                    Column(Modifier.width(164.dp).fillMaxHeight().background(Color.Black.copy(alpha = 0.42f)).verticalScroll(rememberScrollState()).padding(vertical = 6.dp)) {
                        SettingsSection.entries.forEach { item -> SectionButton(item, section == item) { section = item } }
                    }
                    SettingsRows(section, settings, onPage, host, Modifier.weight(1f).fillMaxHeight())
                }
            }
        }
    }
}

@Composable
private fun SectionButton(section: SettingsSection, selected: Boolean, onClick: () -> Unit) {
    Text(
        section.title.uppercase(),
        Modifier.padding(horizontal = 5.dp, vertical = 3.dp).clip(RoundedCornerShape(2.dp))
            .background(if (selected) Blood.copy(alpha = 0.72f) else Color.Transparent)
            .clickable(onClick = onClick).padding(horizontal = 11.dp, vertical = 9.dp),
        fontFamily = OswaldFamily(),
        fontSize = 12.sp,
        letterSpacing = 1.3.sp,
        color = if (selected) Bone else Ash,
        maxLines = 1,
        softWrap = false,
    )
}

@Composable
private fun SettingsRows(
    section: SettingsSection,
    s: LauncherSettings,
    onPage: (LauncherPage) -> Unit,
    host: LauncherHost,
    modifier: Modifier = Modifier,
) {
    var helpKey by remember(section) { mutableStateOf("") }
    Column(modifier.verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp)) {
        Row(Modifier.fillMaxWidth().padding(start = 3.dp, bottom = 5.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(section.title.uppercase(), Modifier.weight(1f), fontFamily = MonoFamily(), fontSize = 11.sp, letterSpacing = 1.2.sp, color = Ash)
            TextButton(onClick = host::resetSettings, contentPadding = PaddingValues(horizontal = 9.dp, vertical = 2.dp)) {
                Text("RESET", fontFamily = OswaldFamily(), fontSize = 10.sp, letterSpacing = 1.2.sp, color = Ash)
            }
        }
        if (section == SettingsSection.Graphics) {
            ChoicePref("Preset", s.presetName, listOf("Low", "Medium", "High", "Custom")) { value ->
                val preset = listOf("Low", "Medium", "High", "Custom").indexOf(value)
                if (preset in 0..2) host.applyPreset(preset)
            }
        }
        s.rows.filter { it.category == section.category }.forEach { row ->
            if (row.key == "touch_layout") {
                ActionPref(row.label, "EDIT", onClick = host::openTouchLayout)
            } else {
                ChoicePref(row.label, row.value, row.options, onSelect = { helpKey = row.key }) { value ->
                    val index = row.options.indexOf(value)
                    if (index >= 0) {
                        helpKey = row.key
                        host.setSetting(row.key, index)
                    }
                }
            }
            if (helpKey == row.key && row.help.isNotBlank()) {
                Text(
                    row.help,
                    Modifier.fillMaxWidth().padding(horizontal = 4.dp, vertical = 2.dp),
                    fontFamily = OswaldFamily(),
                    fontSize = 11.sp,
                    lineHeight = 14.sp,
                    color = Ash,
                )
            }
        }
        if (section == SettingsSection.Graphics) {
            ActionPref("Driver packages", "OPEN", onClick = { onPage(LauncherPage.Drivers) })
        }
        Spacer(Modifier.height(12.dp))
    }
}

@Composable
private fun SettingRow(label: String, onSelect: () -> Unit = {}, control: @Composable () -> Unit) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 49.dp)
            .onFocusChanged { if (it.isFocused) onSelect() }
            .clickable(onClick = onSelect)
            .padding(horizontal = 3.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text(
            label,
            Modifier.weight(1f),
            fontFamily = OswaldFamily(),
            fontSize = 14.sp,
            lineHeight = 17.sp,
            letterSpacing = 0.5.sp,
            color = Bone.copy(alpha = 0.92f),
            softWrap = true,
        )
        Box(Modifier.widthIn(min = 132.dp, max = 216.dp)) { control() }
    }
}

@Composable
private fun ChoicePref(
    label: String,
    value: String,
    options: List<String>,
    onSelect: () -> Unit = {},
    onChange: (String) -> Unit,
) {
    SettingRow(label, onSelect) {
        val current = options.indexOf(value).coerceAtLeast(0)
        Row(
            Modifier.fillMaxWidth().heightIn(min = 38.dp).border(1.dp, Color.White.copy(alpha = 0.16f), RoundedCornerShape(2.dp)).background(Color.Black.copy(alpha = 0.52f)),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("‹", Modifier.clickable { onChange(options[(current - 1 + options.size) % options.size]) }.padding(horizontal = 10.dp, vertical = 5.dp), fontFamily = OswaldFamily(), fontSize = 22.sp, color = BloodBright)
            Text(value, Modifier.weight(1f).padding(horizontal = 1.dp), fontFamily = OswaldFamily(), fontSize = 12.sp, lineHeight = 14.sp, letterSpacing = 0.6.sp, color = Bone, textAlign = TextAlign.Center, softWrap = true, maxLines = 2)
            Text("›", Modifier.clickable { onChange(options[(current + 1) % options.size]) }.padding(horizontal = 10.dp, vertical = 5.dp), fontFamily = OswaldFamily(), fontSize = 22.sp, color = BloodBright)
        }
    }
}

@Composable
private fun ActionPref(label: String, button: String, onClick: () -> Unit) {
    SettingRow(label) { OutlineButton(button, onClick) }
}

@Composable
private fun DriversScreen(settings: LauncherSettings, host: LauncherHost, onBack: () -> Unit) {
    PageFrame("DRIVERS", listOf(Res.drawable.wall_blacklight_portrait, Res.drawable.wall_quarantine_city), onBack) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("CURRENT · ${settings.value("vulkan_driver").uppercase()}", fontFamily = MonoFamily(), fontSize = 11.sp, letterSpacing = 1.sp, color = Ash)
            listOf(
                Triple("Auto", 0, "Choose the compatible driver for this GPU. Recommended for most devices."),
                Triple("System", 1, "Use the Android device Vulkan driver directly."),
                Triple("Turnip", 2, "Use ProtoDroid's bundled Adreno driver; not a Mali driver."),
                Triple("Custom", 3, "Use a validated package selected in the native driver library."),
            ).forEach { (mode, index, description) ->
                ModeRow(mode, description, settings.value("vulkan_driver") == mode) {
                    host.setSetting("vulkan_driver", index)
                }
            }
            ActionButton("MANAGE DRIVER PACKAGES", onClick = host::openDriverPackages)
            Text(
                "Imports, pinned downloads, installation, selection and removal use the existing validated native manager. Changes apply on the next Play.",
                Modifier.padding(top = 4.dp),
                fontFamily = MonoFamily(),
                fontSize = 10.sp,
                color = Ash,
                lineHeight = 14.sp,
            )
        }
    }
}

@Composable
private fun ModeRow(title: String, description: String, selected: Boolean, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(3.dp)).border(1.dp, if (selected) BloodBright else Color.White.copy(alpha = 0.12f), RoundedCornerShape(3.dp))
            .background(if (selected) Blood.copy(alpha = 0.18f) else PanelInk.copy(alpha = 0.78f))
            .clickable(onClick = onClick).padding(12.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Column(Modifier.weight(1f)) {
            Text(title.uppercase(), fontFamily = OswaldFamily(), fontSize = 16.sp, letterSpacing = 1.2.sp, color = Bone)
            Text(description, fontFamily = OswaldFamily(), fontSize = 12.sp, lineHeight = 15.sp, color = Ash)
        }
        Text(if (selected) "●" else "○", fontFamily = MonoFamily(), fontSize = 16.sp, color = if (selected) BloodBright else Ash)
    }
}

@Composable
private fun ModsScreen(mods: List<LauncherMod>, host: LauncherHost, onBack: () -> Unit) {
    PageFrame("MODS", binkScenes, onBack) {
        Column(
            Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text("${mods.count { it.enabled && it.error.isBlank() }} ACTIVE", Modifier.weight(1f), fontFamily = MonoFamily(), fontSize = 11.sp, color = Ash)
                OutlineButton("RECHECK", host::refreshMods)
                Spacer(Modifier.width(7.dp))
                ActionButton("MOD LIBRARY", onClick = host::openMods)
            }
            Text(
                "Import, covers, descriptions, options and removal are available in the full native library. Enable changes apply on the next Play.",
                fontFamily = MonoFamily(),
                fontSize = 10.sp,
                lineHeight = 13.sp,
                color = Ash,
            )
            if (mods.isEmpty()) {
                Surface(Modifier.fillMaxWidth(), color = PanelInk.copy(alpha = 0.86f), shape = RoundedCornerShape(3.dp), border = BorderStroke(1.dp, Color.White.copy(alpha = 0.1f))) {
                    Text("No local mods installed. Open the mod library to import a package.", Modifier.padding(16.dp), fontFamily = OswaldFamily(), fontSize = 14.sp, color = Bone)
                }
            }
            mods.forEach { mod ->
                Column(Modifier.fillMaxWidth().background(PanelInk.copy(alpha = 0.88f)).padding(11.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(mod.name, fontFamily = OswaldFamily(), fontSize = 14.sp, color = Bone)
                            Text(mod.kind, fontFamily = MonoFamily(), fontSize = 10.sp, color = Ash, lineHeight = 13.sp)
                        }
                        if (mod.hasOptions) Text("OPTIONS IN LIBRARY", fontFamily = MonoFamily(), fontSize = 9.sp, color = Ash)
                        Spacer(Modifier.width(8.dp))
                        OutlineButton(if (mod.enabled) "DISABLE" else "ENABLE") {
                            host.setModEnabled(mod.id, !mod.enabled)
                        }
                    }
                    if (mod.detail.isNotBlank()) {
                        Text(mod.detail, Modifier.padding(top = 5.dp), fontFamily = OswaldFamily(), fontSize = 11.sp, lineHeight = 14.sp, color = Bone.copy(alpha = 0.76f))
                    }
                    if (mod.error.isNotBlank()) {
                        Text(mod.error, Modifier.padding(top = 5.dp), fontFamily = MonoFamily(), fontSize = 10.sp, lineHeight = 13.sp, color = BloodBright)
                    }
                }
            }
        }
    }
}

@Composable
private fun LayoutScreen(host: LauncherHost, onBack: () -> Unit) {
    PageFrame("TOUCH LAYOUT", listOf(Res.drawable.wall_mercer_city, Res.drawable.wall_blacklight_portrait), onBack) {
        Box(Modifier.fillMaxSize().padding(18.dp), contentAlignment = Alignment.Center) {
            Surface(
                Modifier.fillMaxWidth().widthIn(max = 620.dp),
                color = PanelInk.copy(alpha = 0.9f),
                shape = RoundedCornerShape(3.dp),
                border = BorderStroke(1.dp, Color.White.copy(alpha = 0.12f)),
            ) {
                Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    Text("EDIT THE REAL CONTROL LAYOUT", fontFamily = DirtFamily(), fontSize = 25.sp, color = Bone)
                    Text(
                        "Move, resize, hide, remove or add controls. The native editor retains bindings, editable text, mouse choices, polygon snapping and stick restoration.",
                        fontFamily = OswaldFamily(),
                        fontSize = 13.sp,
                        lineHeight = 17.sp,
                        color = Bone.copy(alpha = 0.8f),
                    )
                    ActionButton("OPEN TOUCH EDITOR", onClick = host::openTouchLayout)
                    Text("ALSO AVAILABLE AT SETTINGS → CONTROLS → TOUCH LAYOUT", fontFamily = MonoFamily(), fontSize = 9.sp, color = Ash)
                }
            }
        }
    }
}

@Composable
private fun LogsScreen(document: LauncherLogDocument, host: LauncherHost, onBack: () -> Unit) {
    LaunchedEffect(Unit) { host.refreshLog() }
    PageFrame("LOGS", listOf(Res.drawable.wall_manhattan_smoke), onBack) {
        Column(Modifier.fillMaxSize().padding(12.dp)) {
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
                launcherLogIds.forEach { (id, label) ->
                    Text(
                        label.uppercase(),
                        Modifier.clip(RoundedCornerShape(2.dp))
                            .background(if (document.id == id) Blood.copy(alpha = 0.72f) else Color.Transparent)
                            .clickable { host.selectLog(id) }
                            .padding(horizontal = 10.dp, vertical = 8.dp),
                        fontFamily = OswaldFamily(),
                        fontSize = 10.sp,
                        letterSpacing = 1.sp,
                        color = if (document.id == id) Bone else Ash,
                    )
                }
            }
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Text(document.name, Modifier.weight(1f), fontFamily = MonoFamily(), fontSize = 11.sp, color = Ash, maxLines = 1, overflow = TextOverflow.Ellipsis)
                OutlineButton("REFRESH", host::refreshLog)
                Spacer(Modifier.width(7.dp))
                if (document.content.isNotEmpty()) OutlineButton("COPY") { host.copyText(document.content) }
            }
            if (document.path.isNotBlank()) {
                Text(document.path, fontFamily = MonoFamily(), fontSize = 9.sp, color = Ash, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            if (document.truncated) {
                Text("READING THE LAST 512 KiB · COPY USES THIS BOUNDED LOG TAIL", fontFamily = MonoFamily(), fontSize = 9.sp, color = BloodBright)
            }
            val visibleContent = remember(document.id, document.content) {
                if (document.content.length > 160_000) {
                    "… Older visible lines omitted for smooth scrolling; Copy retains the complete bounded tail. …\n" +
                        document.content.takeLast(160_000)
                } else document.content
            }
            Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(top = 8.dp)) {
                when {
                    document.error.isNotBlank() -> Text(document.error, fontFamily = MonoFamily(), fontSize = 11.sp, color = BloodBright)
                    visibleContent.isBlank() -> Text("Log is empty", fontFamily = MonoFamily(), fontSize = 11.sp, color = Ash)
                    else -> Text(visibleContent, Modifier.fillMaxWidth(), fontFamily = MonoFamily(), fontSize = 10.sp, lineHeight = 14.sp, color = Bone.copy(alpha = 0.82f), softWrap = true)
                }
            }
        }
    }
}

@Composable
private fun AboutScreen(
    about: LauncherAbout,
    update: LauncherUpdateState,
    host: LauncherHost,
    onBack: () -> Unit,
) {
    PageFrame("ABOUT", listOf(Res.drawable.wall_ashen_profile, Res.drawable.wall_mercer_urban), onBack) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Image(painterResource(Res.drawable.launcher_icon), null, Modifier.size(54.dp).clip(RoundedCornerShape(10.dp)))
                Column {
                    Text("PROTODROID ${about.versionName}", fontFamily = OswaldFamily(), fontSize = 20.sp, letterSpacing = 1.5.sp, color = Bone)
                    Text("NATIVE ANDROID PORT", fontFamily = MonoFamily(), fontSize = 10.sp, letterSpacing = 1.5.sp, color = Ash)
                }
            }
            Text("Bring your own game data. No game files or DRM bypass are included.", fontFamily = MonoFamily(), fontSize = 10.sp, lineHeight = 14.sp, color = Ash)
            Surface(
                Modifier.fillMaxWidth(),
                color = PanelInk.copy(alpha = 0.86f),
                shape = RoundedCornerShape(3.dp),
                border = BorderStroke(1.dp, Color.White.copy(alpha = 0.1f)),
            ) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
                    Text("UPDATES", fontFamily = MonoFamily(), fontSize = 11.sp, letterSpacing = 1.2.sp, color = Ash)
                    if (update.visible) {
                        Text(
                            when (update.status) {
                                "checking" -> "Checking for updates"
                                "available" -> "Update ${update.versionName} available"
                                "downloading" -> "Downloading update ${update.versionName}" +
                                    if (update.progress >= 0) " · ${update.progress}%" else ""
                                "verifying" -> "Verifying update ${update.versionName}"
                                "ready" -> "Update ${update.versionName} ready to install"
                                else -> "Update needs attention"
                            },
                            fontFamily = OswaldFamily(),
                            fontSize = 14.sp,
                            color = Bone,
                        )
                    }
                    if (update.detail.isNotBlank()) Text(update.detail, fontFamily = OswaldFamily(), fontSize = 12.sp, lineHeight = 15.sp, color = Ash)
                    Row(
                        Modifier.horizontalScroll(rememberScrollState()),
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        OutlineButton("CHECK UPDATES", host::checkUpdates)
                        if (update.visible && update.canPress) {
                            ActionButton(
                                when (update.status) {
                                    "available" -> "OPEN RELEASE"
                                    "ready" -> "INSTALL"
                                    else -> "RETRY"
                                },
                                onClick = host::updateNow,
                            )
                        }
                        OutlineButton("ALL RELEASES", host::openReleases)
                    }
                    Text("Previous versions remain available in the official release archive.", fontFamily = MonoFamily(), fontSize = 9.sp, color = Ash)
                }
            }
            Surface(Modifier.fillMaxWidth(), color = PanelInk.copy(alpha = 0.76f), shape = RoundedCornerShape(3.dp)) {
                Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(about.device, fontFamily = OswaldFamily(), fontSize = 14.sp, color = Bone)
                    Text("${about.os} · ${about.gpu}", fontFamily = MonoFamily(), fontSize = 10.sp, color = Ash)
                    Text("CPU ${about.cpu} · RAM ${about.ram}", fontFamily = MonoFamily(), fontSize = 10.sp, color = Ash)
                    Text("ABI ${about.abi}", fontFamily = MonoFamily(), fontSize = 10.sp, color = Ash)
                    Text("SCREEN ${about.screen}", fontFamily = MonoFamily(), fontSize = 10.sp, color = Ash)
                    Text("DEVICE BUILD ${about.nativeBuild}", fontFamily = MonoFamily(), fontSize = 9.sp, lineHeight = 12.sp, color = Ash)
                }
            }
        }
    }
}

@Composable
private fun UpdateWarning(version: String, host: LauncherHost) {
    Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = 0.88f)), contentAlignment = Alignment.Center) {
        Surface(
            Modifier.fillMaxWidth(0.82f).widthIn(max = 560.dp),
            color = PanelInk,
            shape = RoundedCornerShape(3.dp),
            border = BorderStroke(1.dp, BloodBright.copy(alpha = 0.55f)),
        ) {
            Column(Modifier.padding(18.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Text("SIGNING CERTIFICATE CHANGED", fontFamily = DirtFamily(), fontSize = 24.sp, color = Bone)
                Text(
                    "ProtoDroid $version is official, but its signing certificate differs from this installation. Android must remove the old app before installing it.",
                    fontFamily = OswaldFamily(),
                    fontSize = 13.sp,
                    lineHeight = 17.sp,
                    color = Bone.copy(alpha = 0.84f),
                )
                Text(
                    "Shared game files and saves in /Prototype stay in place. Private settings may reset. The verified APK remains in Downloads.",
                    fontFamily = MonoFamily(),
                    fontSize = 10.sp,
                    lineHeight = 14.sp,
                    color = Ash,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlineButton("CANCEL", host::dismissUpdateWarning)
                    ActionButton("UNINSTALL & INSTALL", onClick = host::confirmUpdate)
                }
            }
        }
    }
}

@Composable
private fun PageFrame(title: String, scenes: List<DrawableResource>, onBack: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    Box(Modifier.fillMaxSize()) {
        CinematicBackdrop(scenes, darken = 0.34f)
        Column(Modifier.fillMaxSize()) {
            Row(
                Modifier.fillMaxWidth().heightIn(min = 46.dp).background(Color.Black.copy(alpha = 0.48f)).padding(horizontal = 12.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text("‹ BACK", Modifier.clip(RoundedCornerShape(2.dp)).clickable(onClick = onBack).padding(horizontal = 8.dp, vertical = 7.dp), fontFamily = OswaldFamily(), fontSize = 12.sp, letterSpacing = 1.2.sp, color = Ash, maxLines = 1)
                Box(Modifier.width(1.dp).height(22.dp).background(Color.White.copy(alpha = 0.18f)))
                Text(title, Modifier.weight(1f), fontFamily = DirtFamily(), fontSize = 22.sp, color = Bone, maxLines = 1, softWrap = false)
            }
            Column(Modifier.weight(1f).fillMaxWidth(), content = content)
            Row(Modifier.fillMaxWidth().heightIn(min = 25.dp).background(Color.Black.copy(alpha = 0.5f)).padding(horizontal = 12.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("TAP TO SELECT", fontFamily = MonoFamily(), fontSize = 9.sp, letterSpacing = 1.sp, color = Ash)
                Spacer(Modifier.weight(1f))
                Text("BACK", fontFamily = OswaldFamily(), fontSize = 9.sp, letterSpacing = 1.sp, color = Bone.copy(alpha = 0.7f))
            }
        }
    }
}

@Composable
private fun StatusTag(text: String, color: Color) {
    Text(text, Modifier.border(1.dp, color.copy(alpha = 0.48f), RoundedCornerShape(2.dp)).padding(horizontal = 7.dp, vertical = 4.dp), fontFamily = MonoFamily(), fontSize = 9.sp, letterSpacing = 0.8.sp, color = color, maxLines = 1)
}

@Composable
private fun ActionButton(text: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        modifier = modifier.heightIn(min = 36.dp),
        shape = RoundedCornerShape(2.dp),
        contentPadding = PaddingValues(horizontal = 12.dp, vertical = 7.dp),
        colors = ButtonDefaults.buttonColors(containerColor = Blood, contentColor = Bone),
    ) {
        Text(text, fontFamily = OswaldFamily(), fontSize = 11.sp, letterSpacing = 1.1.sp, maxLines = 1, softWrap = false)
    }
}

@Composable
private fun OutlineButton(text: String, onClick: () -> Unit) {
    Text(
        text,
        Modifier.clip(RoundedCornerShape(2.dp)).border(1.dp, Color.White.copy(alpha = 0.18f), RoundedCornerShape(2.dp)).clickable(onClick = onClick).padding(horizontal = 10.dp, vertical = 8.dp),
        fontFamily = OswaldFamily(),
        fontSize = 10.sp,
        letterSpacing = 1.sp,
        color = Bone,
        maxLines = 1,
        softWrap = false,
    )
}