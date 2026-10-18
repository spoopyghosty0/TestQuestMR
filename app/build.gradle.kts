plugins {
    id("com.android.application")
}

// Release signing comes from the environment (CI secrets or a local key); without it, release builds are signed with
// the debug key.
val releaseKeystore: String? = System.getenv("TQMR_KEYSTORE")

android {
    namespace = "io.github.spoopyghosty0.testquestmr"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "io.github.spoopyghosty0.testquestmr"
        minSdk = 29
        targetSdk = 32
        versionCode = 1
        versionName = "0.1.0"
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    signingConfigs {
        if (releaseKeystore != null) {
            create("release") {
                storeFile = file(releaseKeystore)
                storePassword = System.getenv("TQMR_KEYSTORE_PASSWORD")
                keyAlias = System.getenv("TQMR_KEY_ALIAS")
                keyPassword = System.getenv("TQMR_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release") ?: signingConfigs.getByName("debug")
        }
    }

    buildFeatures {
        prefab = true
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    lint {
        // Quest apps target API 32; this one is never published on Google Play
        disable += "ExpiredTargetSdkVersion"
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }
}

dependencies {
    implementation("org.khronos.openxr:openxr_loader_for_android:1.1.65")
}
