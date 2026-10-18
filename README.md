# Need for Speed: Underground VR

Native VR for **Need for Speed: Underground (2003)** on **Meta Quest 3 standalone** and **Windows PCVR**, using static recompilation of the original Xbox USA release, Vulkan and OpenXR.

Based on [cherepavel/nfsu1-android](https://github.com/cherepavel/nfsu1-android), with VR code adapted from [DR-89/nfsu2-vr](https://github.com/DR-89/nfsu2-vr).

**[Downloads](https://github.com/DR-89/nfsu-vr/releases)** · [Quest installation](#installing-on-meta-quest-3) · [Windows PCVR](#installing-on-windows-pcvr) · [Controls](#controls-and-vr-settings) · [Troubleshooting](#troubleshooting) · [Building](#building-from-source)

> **No original game data is included.** Supply your own compatible Xbox disc dump. Downloads contain the port, its libraries and custom cockpit/hand models, but no original executable, disc image, cars, tracks, textures, music, movies or saves. Game data must be imported before playing.

## Release status

**v0.3.0-preview.1** is the first public preview. Quest 3 is the tested standalone target. Windows builds and passes automated checks; an actual NFSU1 **PCVR headset session has not yet been validated**.

| Release asset | Purpose |
|---|---|
| `NFSU-VR-Quest3-v0.3.0-preview.1.apk` | Standalone Quest 3 application |
| `NFSU-VR-PCVR-Windows-x64-v0.3.0-preview.1.zip` | Experimental Windows x64/OpenXR package |
| `SHA256SUMS.txt` | Download checksums |
| `release-manifest.json` | Matching source commit, version and package contents |

Download the named assets under [Releases](https://github.com/DR-89/nfsu-vr/releases), not GitHub's automatic source-code ZIP, to play. The APK is a signed development build; use builds signed with the same key for in-place updates.

## Features and limitations

- Native ARM64 execution on Quest; native x64 execution on Windows.
- OpenXR stereo and head tracking, multiview rendering and fixed foveation.
- Touch driving and one/two-hand steering by gripping the virtual wheel.
- A custom cockpit slot in the normal camera cycle, per-car fitting and original exterior bodywork.
- Three rear-view mirrors where the original mirror geometry is recognized.
- NFSU1-specific dashboard instruments and a minimap on the center console.
- Pause-menu **VR Settings** for sensitivity, wheel rotation, seat position and resolution.
- Immediate seat preview while paused; the cockpit stays visible and mirrors continue working after resuming.
- Local data import with file verification and preservation of existing saves on reimport.

Quest users have confirmed Quick Race, basic driving, the accepted cockpit fit, all three Golf mirrors, the corrected Eclipse instruments/map, and the pause/resume and seat-preview fixes. The common interior is a fitted prototype, not a different authentic interior for every car. Geometry checks cover 20 stock cars; Focus/Tiburon center mirrors and broader live testing of tuned parts and other cars remain open.

A 64-second Golf sample at **1680 × 1760 per eye** averaged **44.73 FPS**, with samples from 33.7 to 61.3 FPS. This is not a guaranteed frame rate or controlled benchmark. Stable 72 FPS, all race modes, long sessions, audio behavior, every secondary input and PCVR hardware validation remain work in progress. Full applicable NFSU2 VR feature parity is the goal, not a completed claim. See [FEATURE_PARITY.md](FEATURE_PARITY.md).

## Required game version

Use the complete, unmodified **original Xbox USA / NTSC-U release of Underground 1**.

| Check | Supported value |
|---|---|
| Xbox title ID | `45410047` |
| Executable | `default.xbe`, 2,473,984 bytes |
| SHA-256 | `f4b3dc578fa38abb59961c0c1c692f28514b2ca8bfbd43854c726a1636b34b22` |
| Installed data | 417 verified files, about 1.9 GB |

PC, PlayStation 2, GameCube, European/PAL editions, Underground 2 and modified executables are unsupported. Renaming an extension does not convert an edition.

The extracted disc root must contain:

```text
default.xbe
CARS/
FRONTEND/
GLOBAL/
LANGUAGES/
MEMCARD/
MOVIES/
SOUND/
TRACKS/
```

Keep all files in those directories. Import validates filenames, sizes and hashes against [the supported-disc manifest](config/game-manifest.json), which contains metadata only. Your original source image/archive is not deleted or patched.

## Installing on Meta Quest 3

### 1. Get the APK and prepare your data

Download the Quest APK from [Releases](https://github.com/DR-89/nfsu-vr/releases). Have a computer, a USB-C **data** cable and your own supported Xbox dump ready.

| Supported Quest input | Contents |
|---|---|
| `.iso` / `.xiso` | A complete supported Xbox disc image; supported full-disc images are read directly. |
| `.zip` / `.7z` of extracted files | `default.xbe` and every directory listed above. A common enclosing folder is accepted. |
| `.zip` / `.7z` containing a disc | Exactly one supported Xbox ISO/XISO; the app unpacks it automatically. |

Use complete, unencrypted files. RAR and split archives are unsupported: extract them on your computer first. If 7z reports a memory-limit error, repack with a dictionary of 64 MiB or less, use ZIP, or transfer the unpacked ISO.

### 2. Check free storage

After copying the source to the Quest, allow additional room for the private source copy, any unpacked ISO and about 1.9 GB of installed files.

| Source already in Download | Additional space to allow |
|---|---|
| ZIP/7z of extracted files | Archive size + about 2 GB; **5 GB** is a useful allowance for a typical archive. |
| Direct full ISO of about 7.8 GB | ISO size + about 2 GB; allow **11 GB**. |
| 7z containing a full ISO | Archive size + unpacked ISO size + about 2 GB; allow **18 GB** for a roughly 6 GB archive containing a 7.8 GB ISO. |

These are estimates; existing installations and saves also occupy space. An archive of extracted files usually requires less temporary space.

### 3. Enable sideloading

Follow [Meta's device setup guide](https://developers.meta.com/vr/documentation/native/android/mobile-device-setup/) to join/create a developer team and verify your account. In the Meta Horizon phone app, select your paired headset and enable **Headset Settings → Developer Mode**. On Windows, install the Oculus ADB Drivers linked by Meta.

Connect the powered-on headset using the data cable. Put it on and accept **Allow USB debugging**. You can choose to trust your own computer permanently. Menu locations can vary with app/OS updates.

### 4. Install the APK

**SideQuest:** Install the desktop Advanced Installer from [SideQuest's setup page](https://sidequestvr.com/setup-howto). Wait for the Quest to appear connected and authorized. Use **Install APK** to select the downloaded APK, then wait for completion.

**ADB alternative:** Extract Google's [Android SDK Platform Tools](https://developer.android.com/tools/releases/platform-tools), open PowerShell in that folder, and run:

```powershell
.\adb.exe devices
.\adb.exe -s YOUR_QUEST_SERIAL install -r "C:\Downloads\NFSU-VR-Quest3-v0.3.0-preview.1.apk"
```

Replace the serial and file path. The device state must be `device`; if it is `unauthorized`, accept the headset prompt. Wait for `Success`. On macOS/Linux use `./adb` and adjust paths. The `-r` option preserves existing app data; see [the ADB reference](https://developer.android.com/tools/adb).

### 5. Copy the ISO or archive

Put the source in shared **Download** storage (`/sdcard/Download/`), sometimes displayed as **Downloads**. Choose one method:

- **File Explorer:** Accept the headset's separate USB file-access prompt, then copy to **This PC → Quest 3 → Internal shared storage → Download**. If necessary, enable **MTP Notification** in headset Developer settings and reconnect, as described in Meta's guide.
- **SideQuest:** Use its file manager to upload into `sdcard/Download`. The APK installer is for the app, not the game archive.
- **ADB:** Run the command matching your source:

```powershell
# Direct ISO:
.\adb.exe -s YOUR_QUEST_SERIAL push "C:\Games\NFSU-USA.iso" /sdcard/Download/NFSU-USA.iso

# Or a supported archive:
.\adb.exe -s YOUR_QUEST_SERIAL push "C:\Games\NFSU-USA.7z" /sdcard/Download/NFSU-USA.7z
```

Wait for the entire transfer. No manual copying into `Android/data` is needed.

### 6. Import and play

1. Open the Quest library's **Unknown Sources** section and launch **NFSU1 Quest Development**. First launch opens setup. **NFSU1 · Game data** also opens setup directly.
2. Press **Select archive**, browse to **Download/Downloads**, and select the ISO, XISO, ZIP or 7z.
3. Keep setup open while it extracts and verifies the files. Large 7z archives can take several minutes. Progress and **Cancel import** are available.
4. After import completes, press **Start game**. Future launches through **NFSU1 Quest Development** enter VR directly.
5. Start a Quick Race. Click the **right stick** through the normal views to reach the cockpit; click **both sticks** together to recenter.
6. You can disconnect the computer. After successful import, delete the source from the Quest's Download folder if you want to recover space; keep your original dump on the PC.

### 7. Adjust the seat and resolution

Pause with the left controller's menu button, then press **X** at **[X] VR SETTINGS**. Use the stick to choose a row and adjust it.

Seat changes are visible immediately while paused. Positive height moves your eyes up; positive distance moves them farther back. Press **B** or choose **Save and Back**, then resume.

Start with **1680 × 1760 per eye** on Quest 3, or try **1280 × 1344** for less rendering load. Resolution changes take effect after saving and restarting the game. Other settings survive restart too.

## Installing on Windows PCVR

### 1. Prepare the PC and headset

You need 64-bit Windows, a Vulkan-capable graphics driver and a configured **OpenXR runtime**. If Windows reports missing `VCRUNTIME140`/`MSVCP140` libraries, install Microsoft's [Visual C++ x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

Connect the headset to its PCVR service first. Quest connection options include Quest Link/Air Link or Virtual Desktop with VDXR. Select the desired OpenXR runtime in that service's settings. The port uses the selected runtime and does not change the OpenXR registry. An end-to-end NFSU1 headset test is still pending.

### 2. Extract the complete ZIP

Download the Windows ZIP and extract **all** files into a writable directory such as `C:\Games\NFSU-VR`. Do not run from inside the ZIP or move only the EXE.

```text
NFSU-VR-PCVR-Windows-x64-v0.3.0-preview.1/
├── Start-PCVR.cmd
├── nfsu1-pcvr.exe
├── SDL2.dll
├── openxr_loader.dll
├── pcvr.env
├── cockpits/
├── README.txt
├── INSTALLATION.md
├── FEATURE_PARITY.md
├── LICENSE
└── notices/
```

The launcher creates `game/` and `logs/` as needed. No game content is shipped in either directory.

### 3. Import game data

1. Unpack your ISO on the computer if it is inside ZIP/7z. The Windows importer accepts **ISO/XISO**, not compressed archives.
2. Run **Start-PCVR.cmd**. On first launch, the **Game data setup** window appears.
3. Press **Select ISO...** and choose your supported Xbox USA disc.
4. Allow at least **2.5 GB free** in addition to the source ISO. The importer reads the image directly, verifies all 417 files and preserves existing saves if it replaces an installation.
5. VR starts after verification. Future launches use the installed files.

Alternatively, put the complete extracted disc directly in `game/` beside the launcher. The correct path is `game/default.xbe`, not `game/NFSU/default.xbe`.

The supplied launcher uses its adjacent `game/`. Starting the EXE directly can use a complete local `game/` or `%LOCALAPPDATA%\NFSU1VR\game`. Use the launcher consistently.

### 4. Settings and diagnostics

`pcvr.env` contains the current experimental development profile. The in-game VR settings are shared with Quest. Windows preferences use SDL's `%APPDATA%\OpenSourceDevDirk\NFSU1VR` directory. Launcher logs are in the adjacent `logs/` directory.

When reporting a problem, include the release, GPU/driver, OpenXR runtime and relevant `logs/stderr.log` entries. Do not include your game files or saves.

## Controls and VR Settings

This is the implemented Touch mapping. Basic driving/camera/menu operation is confirmed on Quest; secondary actions still need broader NFSU1 validation.

| Action | Touch control |
|---|---|
| Steer | Left stick or grip the virtual wheel near its rim |
| Accelerate | Right trigger |
| Brake / reverse | Left trigger |
| Nitro | A |
| Handbrake | B |
| Manual gear up / down | Right stick up / down |
| Cycle camera, including cockpit | Right stick click |
| Recenter | Both stick clicks together |
| Pause | Left controller menu button |
| Open VR Settings | X in the game's root pause menu |
| Navigate / adjust settings | Stick or D-pad |
| Confirm / save and return | A / B |

Motion steering supports one or two hands. Wheel input compensates for the original game's stick deadzone without changing physical-stick input. **Wheel Rotation** means total lock-to-lock travel; lower values require less hand rotation. Higher **Steering Sensitivity** produces quicker turns.

SDL gamepad support and haptics are integrated but need more hardware testing. Force-feedback racing-wheel support is not advertised by this preview.

## Updates and saves

**Quest:** Install over the current APK using SideQuest or `adb install -r`, with the same signing key. **Uninstalling or clearing app storage deletes imported data, saves and settings.** To replace game data, close the game and use **NFSU1 · Game data → Reimport game data**; saves are preserved.

**Windows:** Close the game and back up your `game/` folder and settings before updating. Extract new binaries into the same location, retaining `game/`. If moving to a new folder, copy your existing `game/` too. The release ZIP contains no saves.

Verify downloads against the release's `SHA256SUMS.txt`:

```powershell
Get-FileHash .\NFSU-VR-Quest3-v0.3.0-preview.1.apk -Algorithm SHA256
Get-FileHash .\NFSU-VR-PCVR-Windows-x64-v0.3.0-preview.1.zip -Algorithm SHA256
```

## Troubleshooting

| Problem | What to check |
|---|---|
| Quest absent from SideQuest/ADB | Developer Mode, USB debugging authorization, Windows ADB driver and a working data cable/port. |
| Storage absent from File Explorer | File access is separate from debugging. Accept the USB notification or transfer with ADB/SideQuest. |
| APK copied but no app appears | Install with SideQuest's APK control or `adb install -r`, then check Unknown Sources. |
| Source missing from picker | Finish copying, then browse to internal storage → Download instead of only Recent. |
| Import runs out of storage | Include the temporary source copy, unpacked ISO, installed files and existing data in your space budget. |
| Executable/hash mismatch | Use complete, unmodified Xbox USA Underground **1** data. PC/PAL/NFSU2 editions are incompatible. |
| 7z memory error | Use a 64 MiB-or-smaller dictionary, ZIP, or the unpacked ISO. |
| Import says game is running | Close the game and select **Check again**. |
| APK signature mismatch | Use a build signed with the installed key. A self-built APK normally has another key; preserve data before switching. |
| Low Quest FPS | Try 1680 × 1760 or 1280 × 1344, save and restart. Higher resolutions cost more; 72 FPS is not guaranteed. |
| Cockpit missing | Cycle the camera while racing. The main-menu car preview does not use the added cockpit. |
| VR Settings missing | Open the original game's root pause menu, not the Quest system menu or an Options subpage, then press X. |
| Windows rejects 7z | Unpack it first; Windows setup accepts ISO/XISO. |
| Missing PC DLLs | Extract the complete ZIP; install the Visual C++ x64 runtime if needed. Vulkan comes from the GPU driver. |
| OpenXR initialization fails | Connect the headset, start its PCVR service and select the intended runtime. Read `logs/stderr.log`. |
| Wrong GPU on a laptop | Assign `nfsu1-pcvr.exe` to the high-performance GPU in Windows graphics settings. |

## Building from source

The repository contains port sources and custom cockpit assets, **not generated retail code**. Generate privately from your own supported disc. Do not commit `game/`, `gen/`, extracted fixtures, captures or saves.

### Common Windows preparation

Install Git, Python 3, CMake 3.24+ and the target toolchain:

```powershell
git clone https://github.com/DR-89/nfsu-vr.git
cd nfsu-vr
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/quest-requirements.txt

# Put your extracted Xbox USA disc in game/ first.
.\.venv\Scripts\python.exe tools/inspect_game.py game
.\.venv\Scripts\python.exe tools/regen.py game
```

Generation requires an empty `gen/`. Build scripts fetch and verify SDL 2.32.10, OpenXR SDK 1.1.53 and glslang 15.1.0 from [quest/dependencies.json](quest/dependencies.json). Old phone/Switch submodules are not needed for the VR targets.

### Quest

Install Android SDK platform 35, Build Tools 35.0.0, NDK 29.0.14206865, JDK 17+ and Gradle 8.11.1. Replace these paths:

```powershell
.\tools\build-quest.ps1 `
  -Sdk "C:\Android\Sdk" `
  -Ndk "C:\Android\Sdk\ndk\29.0.14206865" `
  -Gradle "C:\Tools\gradle-8.11.1\bin\gradle.bat" `
  -JavaHome "C:\Program Files\Android\Android Studio\jbr" `
  -Jobs 4
```

Output: `quest/app/build/outputs/apk/debug/app-debug.apk`. Your local development signing key is not distributed and may differ from the published key. See [Quest development notes](quest/README.md).

### Windows PCVR

Install Visual Studio 2022's Desktop development with C++ workload, Windows SDK and Vulkan headers. Your graphics driver must supply `vulkan-1.dll`.

```powershell
.\tools\build-pcvr.ps1 -VulkanHeaders "C:\VulkanSDK\YOUR_VERSION\Include" -Jobs 4
.\tools\start-pcvr.ps1
```

Output: `build/pcvr/game/nfsu1-pcvr.exe`. `tools/start-pcvr.ps1 -DesktopOnly` runs the flat diagnostic renderer; this does not validate PCVR. See [PCVR development notes](pcvr/README.md).

### Package without game data

After building both targets and committing the source:

```powershell
.\.venv\Scripts\python.exe tools/package-release.py --version v0.3.0-preview.1
```

The tool uses an explicit runtime-file list, inspects APK contents, rejects retail filenames/hashes and private paths, and writes packages, checksums and a source-revision manifest to `dist/`. It excludes game data, saves, generated C, debug symbols and signing keys.

## Credits and license

- [cherepavel/nfsu1-android](https://github.com/cherepavel/nfsu1-android), base `0caa485`: NFSU1 recompilation and Android/runtime work.
- [DR-89/nfsu2-vr](https://github.com/DR-89/nfsu2-vr): VR renderer, hosts, cockpit, controls, settings and importer foundation.
- [antoxa2584x/nfsu1-sw](https://github.com/antoxa2584x/nfsu1-sw): additional NFSU1 reference work.
- [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp) and contributors: recompilation/runtime tools.
- SDL, Khronos OpenXR/glslang, Apache Commons Compress and XZ for Java: bundled dependency notices retain their terms.

The base's [GPL-3.0 license](LICENSE) is retained. Other components retain their own notices, including [xboxrecomp/NOTICE](xboxrecomp/NOTICE). Custom cockpit/glove models were generated for the VR project and contain no extracted game geometry or textures; see [asset provenance](assets/cockpits/README.md).

This is an unofficial community project, not an EA or Meta product. Original game content belongs to its respective owners and must be supplied separately.
