plugins { id("com.android.application") }

android {
    namespace = "dev.frameroom.samplegame"
    compileSdk = 35

    defaultConfig {
        applicationId = "dev.frameroom.samplegame"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "1.0-test"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
