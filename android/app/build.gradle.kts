import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Saját aláíró kulcs adatai: android/keystore.properties (nem kerül a forráscsomagba).
// Ha nincs ilyen fájl, a release a fejlesztői próbakulccsal készül (csak saját használatra).
val keyProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}
val haveKey = keyProps.getProperty("storeFile") != null

android {
    namespace = "hu.ionoprop.app"
    compileSdk = 36

    defaultConfig {
        applicationId = "hu.ionoprop.app"
        minSdk = 24
        targetSdk = 36
        versionCode = 12
        versionName = "0.9.1"
        ndk {
            // telefonok (arm64, régebbi arm) és az emulátor (x86_64)
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
    }

    signingConfigs {
        if (haveKey) {
            create("upload") {
                storeFile = rootProject.file(keyProps.getProperty("storeFile"))
                storePassword = keyProps.getProperty("storePassword")
                keyAlias = keyProps.getProperty("keyAlias")
                keyPassword = keyProps.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName(if (haveKey) "upload" else "debug")
            // a Play Console így tud olvasható hibajelentést adni a C-mag összeomlásairól
            ndk { debugSymbolLevel = "SYMBOL_TABLE" }
        }
    }

    // a kész APK neve: ionoprop-<verzió>.apk (debug: ionoprop-<verzió>-debug.apk)
    applicationVariants.all {
        val v = this
        outputs.all {
            val suffix = if (v.buildType.name == "debug") "-debug" else ""
            (this as com.android.build.gradle.internal.api.BaseVariantOutputImpl).outputFileName =
                "ionoprop-${v.versionName}$suffix.apk"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
