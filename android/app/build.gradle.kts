import java.util.Properties

plugins { id("com.android.application") }

// Run the recompilation tools directly; no shell preparation scripts are needed.
abstract class RecompServices @javax.inject.Inject constructor(
    val processes: org.gradle.process.ExecOperations
)
val recompServices = objects.newInstance<RecompServices>()
val repository = rootProject.projectDir
val localSettings = Properties().apply {
    val settingsFile = rootProject.file("local.properties")
    if (settingsFile.isFile) settingsFile.inputStream().use { load(it) }
}
val xbe = providers.gradleProperty("nfsu.xbe")
    .orElse(provider { localSettings.getProperty("nfsu.xbe", "game/Need For Speed Underground/default.xbe") })
    .map { repository.resolve(it) }
val pythonCommand = providers.gradleProperty("nfsu.python").orElse("python3")
val pythonHome = layout.buildDirectory.dir("recomp-python")
val python = pythonHome.map { it.file(if (System.getProperty("os.name").startsWith("Windows")) "Scripts/python.exe" else "bin/python").asFile }
val generatedCode = layout.buildDirectory.dir("generated/recomp")
val analysis = layout.buildDirectory.dir("recomp-analysis")

val prepareRecompPython = tasks.register("prepareRecompPython") {
    inputs.file(repository.resolve("tools/requirements-recomp.txt"))
    inputs.property("pythonCommand", pythonCommand)
    outputs.dir(pythonHome)
    doLast {
        recompServices.processes.exec {
            commandLine(pythonCommand.get(), "-m", "venv", pythonHome.get().asFile.absolutePath)
        }
        recompServices.processes.exec {
            commandLine(python.get(), "-m", "pip", "install", "-r", repository.resolve("tools/requirements-recomp.txt"))
        }
    }
}

val generateRecomp = tasks.register("generateRecomp") {
    group = "build"
    description = "Generate native C sources from the original Xbox executable."
    dependsOn(prepareRecompPython)
    inputs.file(xbe).withPropertyName("xbe")
    inputs.file(repository.resolve("config/seed_functions.json"))
    inputs.file(repository.resolve("src/recomp_manual.c"))
    inputs.file(repository.resolve("xboxrecomp/templates/runtime/recomp_types.h"))
    inputs.files(fileTree(repository.resolve("xboxrecomp/tools")) {
        include("**/*.py", "**/*.json", "**/*.toml", "**/*.yaml")
        exclude("**/output/**", "**/__pycache__/**")
    })
    outputs.dir(generatedCode)
    outputs.dir(analysis)
    doLast {
        val executable = xbe.get().absolutePath
        val work = analysis.get().asFile
        val disasm = work.resolve("disasm").absolutePath
        val identified = work.resolve("func-id").absolutePath
        val abi = work.resolve("abi").absolutePath
        val generated = generatedCode.get().asFile
        work.mkdirs()
        // Always remove old translation units and the register-model header.
        generated.deleteRecursively()
        generated.mkdirs()
        fun runTool(module: String, vararg arguments: String) {
            recompServices.processes.exec {
                workingDir(repository.resolve("xboxrecomp"))
                commandLine(listOf(python.get().absolutePath, "-m", "tools.$module", executable) + arguments)
            }
        }
        runTool("xbe_parser", "--json", work.resolve("xbe-analysis.json").absolutePath)
        runTool("disasm", "--analysis-json", work.resolve("xbe-analysis.json").absolutePath,
            "--seed-functions", repository.resolve("config/seed_functions.json").absolutePath, "-o", disasm)
        runTool("func_id", "--functions", "$disasm/functions.json", "--strings", "$disasm/strings.json",
            "--xrefs", "$disasm/xrefs.json", "--output", identified)
        runTool("abi_analysis", "--disasm-dir", disasm, "--func-id-dir", identified, "--output-dir", abi)
        runTool("recomp", "--game-name", "NFS Underground", "--all", "--split", "250",
            "--gen-dir", generated.absolutePath, "--disasm-dir", disasm, "--func-id-dir", identified,
            "--abi-dir", abi, "--exclude-manual", repository.resolve("src/recomp_manual.c").absolutePath,
            "--mmio-sections", "DSOUND,XPP")
    }
}

val buildCommit = providers.exec {
    workingDir(repository)
    commandLine("git", "rev-parse", "HEAD")
}.standardOutput.asText.map { it.trim() }

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
    buildFeatures { buildConfig = true }
    defaultConfig {
        applicationId = "org.nfsu.android"
        minSdk = 30
        targetSdk = 37
        versionCode = 1
        versionName = "0.1.0"
        buildConfigField("String", "BUILD_COMMIT", "\"${buildCommit.get().take(12)}\"")
        buildConfigField("String", "BUILD_COMMIT_FULL", "\"${buildCommit.get()}\"")
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DNFSU2_VULKAN=ON", "-DANDROID_STL=c++_shared",
                    "-DNFSU2_GEN_DIR=${generatedCode.get().asFile.absolutePath}")
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

// CMake configuration reads the generated source list, so generation must finish
// before configuration itself, including when building directly from Android Studio.
tasks.configureEach {
    if (name.startsWith("configureCMake") || name.startsWith("buildCMake") || name.startsWith("externalNativeBuild")) {
        dependsOn(generateRecomp)
    }
}
