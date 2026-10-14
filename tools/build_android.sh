#!/usr/bin/env bash
# Build the Android ARM64 APK. Retail data and generated code remain local.
set -euo pipefail
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-21-openjdk}"
if [[ ! -x "$JAVA_HOME/bin/java" ]]; then
    echo "Set JAVA_HOME to an installed JDK 21 or newer." >&2
    exit 1
fi
if [[ ! -f third_party/SDL/CMakeLists.txt || ! -f third_party/glslang/CMakeLists.txt ]]; then
    echo "Initialise dependencies first: git submodule update --init --recursive" >&2
    exit 1
fi
if [[ ! -f src/recomp/gen/recomp_funcs.h || "${REGENERATE:-0}" == 1 ]]; then
    if [[ ! -x .venv/bin/python ]]; then
        echo "Create .venv and install tools/requirements-recomp.txt first." >&2
        exit 1
    fi
    export PATH="$PROJECT_ROOT/.venv/bin:$PATH"
    export NFSU1_XBE="${NFSU1_XBE:-$PROJECT_ROOT/game/Need For Speed Underground/default.xbe}"
    export NFSU1_GEN_DIR="$PROJECT_ROOT/src/recomp/gen"
    bash tools/regen.sh
fi
cd android
./gradlew :app:assembleDebug "$@"
echo "APK: $PROJECT_ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
