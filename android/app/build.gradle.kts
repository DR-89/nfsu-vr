plugins { id("com.android.application") }

val prepareSdlJava by tasks.registering {
    val source = file("../../third_party/SDL/android-project/app/src/main/java")
    val output = layout.buildDirectory.dir("generated/sdl-java")
    inputs.dir(source)
    outputs.dir(output)
    doLast {
        source.walkTopDown().filter { it.isFile && it.extension == "java" }.forEach { original ->
            val destination = output.get().asFile.resolve(original.relativeTo(source))
            destination.parentFile.mkdirs()
            var text = original.readText()
            if (original.name == "HIDDeviceManager.java") {
                val call = "mContext.registerReceiver(mUsbBroadcast, filter);"
                check(text.contains(call)) { "SDL USB receiver changed: review the Android patch" }
                text = text.replace(call, """
                    if (android.os.Build.VERSION.SDK_INT >= 33) {
                        mContext.registerReceiver(mUsbBroadcast, filter, android.content.Context.RECEIVER_NOT_EXPORTED);
                    } else {
                        mContext.registerReceiver(mUsbBroadcast, filter);
                    }
                """.trimIndent())
                // Lint does not follow the API guard around the legacy call.
                text = text.replace("private void initializeUSB()", "@android.annotation.SuppressLint(\"UnspecifiedRegisterReceiverFlag\")\n    private void initializeUSB()")
            }
            destination.writeText(text)
        }
    }
}

android {
    namespace = "org.nfsu.android"
    compileSdk = 37
    ndkVersion = "30.0.16248370"
    defaultConfig {
        applicationId = "org.nfsu.android"
        minSdk = 30
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DNFSU2_VULKAN=ON", "-DANDROID_STL=c++_shared")
                targets += listOf("nfsu2_recomp", "SDL2")
            }
        }
    }
    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = "4.1.2"
        }
    }
    sourceSets["main"].java.directories.add(layout.buildDirectory.dir("generated/sdl-java").get().asFile.path)
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

tasks.named("preBuild") { dependsOn(prepareSdlJava) }
