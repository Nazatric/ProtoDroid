import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("org.jetbrains.kotlin.multiplatform")
    id("org.jetbrains.kotlin.plugin.compose")
    id("org.jetbrains.compose")
    id("com.android.application")
}

val productionManifest = rootProject.file("AndroidManifest.xml").readText()
fun manifestAttribute(name: String): String = Regex("android:$name=\"([^\"]+)\"")
    .find(productionManifest)?.groupValues?.get(1)
    ?: throw GradleException("Missing android:$name in ${rootProject.file("AndroidManifest.xml")}")

kotlin {
    androidTarget {
        compilerOptions {
            jvmTarget.set(JvmTarget.JVM_11)
        }
    }

    sourceSets {
        commonMain.dependencies {
            implementation(compose.runtime)
            implementation(compose.foundation)
            implementation(compose.material3)
            implementation(compose.ui)
            implementation(compose.components.resources)
            implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.2")
        }
        androidMain.dependencies {
            implementation("androidx.activity:activity-compose:1.10.1")
        }
    }
}

compose.resources {
    publicResClass = true
    packageOfResClass = "com.protodroid.launcher.generated.resources"
}

android {
    namespace = "com.protodroid"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.protodroid"
        minSdk = 26
        targetSdk = 35
        versionCode = manifestAttribute("versionCode").toInt()
        versionName = manifestAttribute("versionName")
        multiDexEnabled = true
    }

    sourceSets {
        getByName("main") {
            manifest.srcFile(rootProject.file("AndroidManifest.xml"))
            java.srcDir(rootProject.file("java"))
            res.srcDir(rootProject.file("res"))
            assets.srcDir(rootProject.file("assets"))

            val sdlRoot = System.getenv("SDL_SRC")
                ?: "${System.getProperty("user.home")}/protodroid-data/android-deps-src/SDL2"
            val sdlJava = file("$sdlRoot/android-project/app/src/main/java")
            if (!sdlJava.isDirectory) {
                throw GradleException("SDL2 Java sources are missing at $sdlJava; run android/build-deps.sh first")
            }
            java.srcDir(sdlJava)
        }
    }

    buildTypes {
        getByName("release") {
            isMinifyEnabled = false
            isShrinkResources = false
            signingConfig = null
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}
