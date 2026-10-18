# NFS Underground VR on Windows

Development host for Windows x64, Vulkan and OpenXR. Build with `tools/build-pcvr.ps1 -VulkanHeaders <Vulkan include directory>` using Visual Studio 2022 C++ tools. Executable: `build/pcvr/game/nfsu1-pcvr.exe`.

`tools/start-pcvr.ps1` selects the configured system OpenXR runtime. Connect Quest Link, Virtual Desktop/VDXR or another compatible PC OpenXR headset before launching. `-DesktopOnly` starts the flat diagnostic renderer. No launcher changes the system OpenXR registry.

Data setup accepts the supported original Xbox USA ISO/XISO and validates all 417 retail files. Unpack 7z first for the Windows importer. Default data folder: complete local `game/`, otherwise `%LOCALAPPDATA%/NFSU1VR/game`; the development launcher supplies the repository's `game/`. VR preferences use SDL's `OpenSourceDevDirk/NFSU1VR` preference directory.

Current evidence: the executable and shared rendering/input tests compile; importer regression passes. Desktop title-screen rendering is verified on the RTX 3050 after correcting hybrid GPU selection; the Vulkan multiview test passes (768 pixels, zero failures). An actual NFSU1 PCVR headset session has not yet been validated. See [feature parity](../FEATURE_PARITY.md).

The shared options retain `NFSU2_*` names. `benchmark.env` is inherited from the reference and is provisional for NFSU1. The script writes diagnostics in `build/pcvr-run`. Source disc and saves must remain outside any source/release archive.

The texture decoder is shared with Quest and passes the same 822-case,
92,252,156-pixel test on both hosts. `NFSU2_VR_TEXTURE_DECODE` overrides the
legacy `NFSU2_PCVR_TEXTURE_DECODE` option; PCVR retains its existing opt-in
default and the first 64 textures of each format are compared pixel by pixel.
