#!/usr/bin/env python3
"""Collect native dependency license texts into the APK's bundled notices."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', type=Path, required=True)
    args = parser.parse_args()
    sources = [
        ('NFSU1 Android base: GPL-3.0', ROOT / 'LICENSE'),
        ('xboxrecomp license', ROOT / 'xboxrecomp/LICENSE'),
        ('xboxrecomp third-party attribution', ROOT / 'xboxrecomp/NOTICE'),
        ('LGPL 2.1', ROOT / 'xboxrecomp/LICENSES/LGPL-2.1.txt'),
        ('SDL', ROOT / 'build/deps/SDL/LICENSE.txt'),
        ('SDL yuv2rgb', ROOT / 'build/deps/SDL/src/video/yuv2rgb/LICENSE'),
        ('SDL HIDAPI', ROOT / 'build/deps/SDL/src/hidapi/LICENSE.txt'),
        ('SDL HIDAPI BSD', ROOT / 'build/deps/SDL/src/hidapi/LICENSE-bsd.txt'),
        ('SDL HIDAPI original', ROOT / 'build/deps/SDL/src/hidapi/LICENSE-orig.txt'),
        ('OpenXR SDK', ROOT / 'build/deps/OpenXR-SDK/LICENSE'),
        ('OpenXR attribution', ROOT / 'build/deps/OpenXR-SDK/COPYING.adoc'),
        ('OpenXR JsonCpp', ROOT / 'build/deps/OpenXR-SDK/src/external/jsoncpp/LICENSE'),
        ('OpenXR jnipp', ROOT / 'build/deps/OpenXR-SDK/src/external/jnipp/LICENSE'),
        ('glslang', ROOT / 'build/deps/glslang/LICENSE.txt'),
        ('LLVM / Android C++ runtime', args.ndk / 'toolchains/llvm/prebuilt/windows-x86_64/NOTICE'),
    ]
    blocks = [
        'Need For Speed Underground VR - Native runtime notices\n\n'
        'NFSU1 base: https://github.com/cherepavel/nfsu1-android\n'
        'VR implementation adapted from https://github.com/DR-89/nfsu2-vr\n'
        'Xbox recompilation runtime: https://github.com/sp00nznet/xboxrecomp\n'
        'Project source and build instructions: https://github.com/DR-89/nfsu-vr\n'
        'Each published release identifies its matching source commit/tag.\n'
        'To rebuild/relink with modified LGPL components, follow the platform build\n'
        'instructions using your own supported disc and locally generated code.\n'
        'Android importer dependency notices are in importer-third-party-notices.txt.\n'
        'The following license texts retain their original terms and attribution.\n'
    ]
    for title, path in sources:
        blocks.append('\n' + '=' * 72 + '\n' + title + '\n' + '=' * 72 + '\n' + path.read_text(encoding='utf-8'))
    output = ROOT / 'quest/app/src/main/assets/runtime-third-party-notices.txt'
    text = '\n'.join(blocks)
    output.write_text('\n'.join(line.rstrip() for line in text.splitlines()).rstrip() + '\n', encoding='utf-8')
    print(f'Runtime notices: {output.name} ({output.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
