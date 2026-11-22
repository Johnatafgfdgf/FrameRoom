plugins { id("com.android.application") }

val stageEmbeddedArena by tasks.registering(Sync::class) {
    dependsOn(":samplegame:assembleDebug")
    from(rootProject.file("samplegame/build/outputs/apk/debug/samplegame-debug.apk")) {
        rename { "arena.apk" }
    }
    into(layout.buildDirectory.dir("generated/arenaAssets"))
}

android {
    namespace = "dev.frameroom"
    compileSdk = 35
    buildFeatures { buildConfig = true }
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "dev.frameroom"
        minSdk = 29
        targetSdk = 35
        versionCode = 12
        versionName = "0.12.0"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON"
            }
        }
    }

    buildTypes {
        getByName("debug") {
            applicationIdSuffix = ".r12"
            resValue("string", "frameroom_label", "FrameRoom R0.12")
        }
        getByName("release") {
            resValue("string", "frameroom_label", "FrameRoom")
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    sourceSets["main"].java.srcDir(rootProject.file("game-api/src/main/java"))
    sourceSets["main"].assets.srcDir(stageEmbeddedArena)

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

// The same cooperative APK is published by Actions and embedded in FrameRoom.
tasks.named("preBuild").configure { dependsOn(stageEmbeddedArena) }
