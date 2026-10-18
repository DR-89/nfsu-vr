#!/usr/bin/env python3
"""Package already-built hosts using explicit file lists; never copy game data."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
REPO = 'https://github.com/DR-89/nfsu-vr'
COCKPITS = ('shared.ncpk', 'shared.json', 'driver-gloves.nghv', 'driver-gloves.json')
APK_LIBS = {'lib/arm64-v8a/' + name for name in (
    'libSDL2.so', 'libnfsu1_quest.so', 'libopenxr_loader.so', 'libc++_shared.so')}
APK_ASSETS = {'assets/cockpits/' + name for name in COCKPITS} | {
    'assets/runtime-third-party-notices.txt', 'assets/importer-third-party-notices.txt'}


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_record(path):
    return {'file': path.name, 'bytes': path.stat().st_size,
            'sha256': digest(path.read_bytes())}


def check_entry(name, data, retail_hashes):
    parts = PurePosixPath(name).parts
    if not parts or name.startswith('/') or '\\' in name or '..' in parts:
        raise ValueError(f'Unsafe archive path: {name}')
    lower = [part.lower() for part in parts]
    private = {'game', 'gen', 'udata', 'tdata', 'saves', 'private', '.git', '.venv'}
    retail = {'cars', 'frontend', 'global', 'languages', 'memcard', 'movies', 'sound', 'tracks'}
    if set(lower) & (private | retail):
        raise ValueError(f'Private/game directory in package: {name}')
    if PurePosixPath(name).suffix.lower() in {
            '.xbe', '.iso', '.xiso', '.7z', '.zip', '.bin', '.pdb', '.log',
            '.jks', '.keystore', '.pfx', '.p12', '.key', '.pem'}:
        raise ValueError(f'Forbidden release payload: {name}')
    if digest(data) in retail_hashes:
        raise ValueError(f'Retail game file content found: {name}')


def audit_zip(path, retail_hashes, allowed=None, apk=False):
    records = []
    with zipfile.ZipFile(path) as archive:
        names = [item.filename for item in archive.infolist() if not item.is_dir()]
        if len(names) != len(set(names)):
            raise ValueError('Duplicate ZIP entries')
        if allowed is not None and set(names) != set(allowed):
            raise ValueError('Package entries differ from the explicit file list')
        if apk:
            assets = {n for n in names if n.startswith('assets/')}
            libs = {n for n in names if n.startswith('lib/')}
            if assets != APK_ASSETS or libs != APK_LIBS:
                raise ValueError(f'Unexpected APK assets/libraries: {assets ^ APK_ASSETS}, {libs ^ APK_LIBS}')
            if any(not (n.startswith(('assets/', 'lib/', 'res/', 'META-INF/')) or
                        n in ('AndroidManifest.xml', 'resources.arsc') or
                        re.fullmatch(r'classes\d*\.dex', n) or
                        re.fullmatch(r'org/apache/commons/codec/language/(?:bm/[a-z_]+|dmrules)\.txt', n)) for n in names):
                raise ValueError('Unexpected APK payload outside Android runtime files')
        for name in names:
            data = archive.read(name)
            check_entry(name, data, retail_hashes)
            records.append({'path': name, 'bytes': len(data), 'sha256': digest(data)})
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'v\d+\.\d+\.\d+(?:-[a-z0-9.]+)?', args.version):
        parser.error('Use a version such as v0.3.0-preview.1')
    if git('status', '--porcelain', '--untracked-files=normal'):
        parser.error('Commit source changes before packaging so the manifest identifies exact sources.')
    commit = git('rev-parse', 'HEAD')
    gradle = (ROOT/'quest/app/build.gradle').read_text(encoding='utf-8')
    if f"versionName '{args.version[1:]}'" not in gradle:
        parser.error('Version does not match quest/app/build.gradle')
    output_metadata = json.loads((ROOT/'quest/app/build/outputs/apk/debug/output-metadata.json').read_text(encoding='utf-8'))
    if any(e['versionName'] != args.version[1:] for e in output_metadata['elements']):
        parser.error('APK build metadata is stale; rebuild before packaging.')
    retail_hashes = {e['sha256'] for e in json.loads((ROOT/'config/game-manifest.json').read_text(encoding='utf-8'))}
    out = ROOT/'dist'/args.version
    if out.exists():
        parser.error(f'Output directory already exists: {out}; preserve or move it before repackaging.')
    apk_source = ROOT/'quest/app/build/outputs/apk/debug/app-debug.apk'
    apk_entries = audit_zip(apk_source, retail_hashes, apk=True)
    payload = {
        'nfsu1-pcvr.exe': ROOT/'build/pcvr/game/nfsu1-pcvr.exe',
        'SDL2.dll': ROOT/'build/pcvr/game/SDL2.dll',
        'openxr_loader.dll': ROOT/'build/pcvr/game/openxr_loader.dll',
        'Start-PCVR.cmd': ROOT/'pcvr/release/Start-PCVR.cmd',
        'pcvr.env': ROOT/'pcvr/benchmark.env',
        'README.txt': ROOT/'pcvr/release/README.txt',
        'INSTALLATION.md': ROOT/'README.md',
        'FEATURE_PARITY.md': ROOT/'FEATURE_PARITY.md',
        'LICENSE': ROOT/'LICENSE',
        'notices/runtime-third-party-notices.txt': ROOT/'quest/app/src/main/assets/runtime-third-party-notices.txt',
        'notices/importer-third-party-notices.txt': ROOT/'quest/app/src/main/assets/importer-third-party-notices.txt',
    }
    payload.update({'cockpits/'+name: ROOT/'assets/cockpits/runtime/cockpits'/name for name in COCKPITS})
    for name, path in payload.items():
        check_entry(name, path.read_bytes(), retail_hashes)
    out.mkdir(parents=True)
    apk = out/f'NFSU-VR-Quest3-{args.version}.apk'
    shutil.copyfile(apk_source, apk)
    folder = f'NFSU-VR-PCVR-Windows-x64-{args.version}'
    pc = out/(folder+'.zip')
    with zipfile.ZipFile(pc, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in sorted(payload.items()):
            archive.write(path, folder+'/'+name)
    pc_entries = audit_zip(pc, retail_hashes, allowed={folder+'/'+name for name in payload})
    files = [file_record(apk), file_record(pc)]
    manifest = {
        'version': args.version, 'source_commit': commit,
        'source_url': REPO+'/tree/'+commit,
        'no_original_game_data': True,
        'validation': 'Explicit runtime file lists; APK asset/library allowlist; retail file hash and path checks.',
        'quest_status': 'Quest 3 gameplay and pause/cockpit/mirror fixes confirmed; development-signed APK.',
        'pcvr_status': 'Experimental; automated and desktop checks passed; headset session not yet validated.',
        'files': files, 'apk_entries': apk_entries, 'pcvr_entries': pc_entries,
    }
    manifest_path = out/'release-manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    sums = files+[file_record(manifest_path)]
    (out/'SHA256SUMS.txt').write_text(''.join(f"{r['sha256']}  {r['file']}\n" for r in sums), encoding='ascii')
    for record in files:
        print(f"{record['file']}: {record['bytes']:,} bytes, SHA-256 {record['sha256']}")
    print(f'Inspected {len(apk_entries)} APK entries and {len(pc_entries)} PCVR entries; no retail payload found.')
    print(f'Source: {commit}\nOutput: {out}')


if __name__ == '__main__':
    main()
