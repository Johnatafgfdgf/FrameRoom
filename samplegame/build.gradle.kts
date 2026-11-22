plugins { id("com.android.application") }

android {
    namespace = "dev.frameroom.samplegame"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.frameroom.samplegame"
        minSdk = 29
        targetSdk = 35
        versionCode = 2
        versionName = "2.0-vulkan-test"
    }

    sourceSets["main"].java.srcDir(rootProject.file("game-api/src/main/java"))

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
