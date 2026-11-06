NFS Underground VR - Windows PCVR preview

Requires Windows x64, a Vulkan driver and a configured OpenXR runtime.
Start-PCVR.cmd -SteamVR selects SteamVR for Steam Link. With no option,
Start-PCVR.cmd uses the user's selected runtime. Connect the PCVR headset first.
Import the original Xbox USA/NTSC-U NFS Underground ISO when prompted.
PC discs and Underground 2 discs are not supported. Unpack 7z archives first.
The original disc image is kept; no retail game data is bundled.

Read INSTALLATION.md for the full installation guide, controls, updates and
troubleshooting. Extract this entire ZIP into a writable folder. Keep the
EXE, DLLs, cockpits directory and pcvr.env together. The launcher imports
data into game/ beside itself and writes diagnostics to logs/.

Place your MP3, FLAC, PCM WAV and H.264 MP4 files in Music/ beside the EXE.
Restart to rescan. Select the soundtrack in the pause-menu VR settings.

This port is under development. See the accompanying FEATURE_PARITY.md for
current implementation and test status. Windows headset startup, focus and stereo frame submission were verified with
Virtual Desktop (VDXR). Extended race and replay playtests are still in progress.

Runtime source attribution: cherepavel/nfsu1-android, DR-89/nfsu2-vr and
sp00nznet/xboxrecomp. See the bundled notices and matching release sources:
https://github.com/DR-89/nfsu-vr
