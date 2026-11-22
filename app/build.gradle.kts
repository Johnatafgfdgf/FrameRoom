plugins { id("com.android.application") }

android {
    namespace = "dev.frameroom"
    compileSdk = 35
    buildFeatures { buildConfig = true }
    ndkVersion = "27.0.12077973"

    defaultConfig {
        applicationId = "dev.frameroom"
        minSdk = 29
        targetSdk = 35
        versionCode = 10
        versionName = "0.10.0"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake { cppFlags += "-std=c++17" }
        }
    }

    externalNativeBuild {
        cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" }
    }
    sourceSets["main"].java.srcDir(rootProject.file("game-api/src/main/java"))

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
