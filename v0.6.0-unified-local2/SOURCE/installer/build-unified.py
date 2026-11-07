"""Build the bilingual offline manager with two complete, deduplicated payloads."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--new', type=Path, required=True)
    parser.add_argument('--legacy', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Use a new output directory.')
    source = Path(__file__).resolve().parent
    args.out.mkdir(parents=True)
    content = args.out / 'content'
    content.mkdir()
    for name in ('Installer-Core.ps1', 'Setup-Manager.ps1'):
        shutil.copy2(source / name, args.out / name)
    shutil.copy2(source / 'Setup-Intel.ps1', args.out / 'Engine.ps1')
    bundle = dict(version='v0.6.0-unified-local1', engine_sha256=sha(args.out / 'Engine.ps1'),
                  previous_host_sha256=[
                      'b1e1bff70ad4251917e6e8cbf3aeb9ec6dcf7d25d4ae0bffbd53cbb300f0ca1c',
                      'ab731be6ac1d63ee0d07cf404c21049dce0ad4cc7b6415f169746e7f92a33483'],
                  variants={})
    for key, package in (('New', args.new), ('Legacy', args.legacy)):
        manifest = json.loads((package / 'PACKAGE-MANIFEST.json').read_text(encoding='utf-8-sig'))
        files = []
        for item in manifest['files']:
            path = package / 'payload' / item['path']
            digest = sha(path)
            if digest.lower() != item['sha256'].lower():
                raise ValueError('Payload checksum mismatch: ' + str(path))
            blob = content / digest
            if not blob.exists():
                shutil.copy2(path, blob)
            files.append(dict(path='payload/' + item['path'], sha256=digest, bytes=path.stat().st_size))
        bundle['variants'][key] = dict(id=key.lower() + '-' + sha(package / 'PACKAGE-MANIFEST.json')[:16],
                                      host_sha256=sha(package / 'payload/OptiScaler.dll'),
                                      manifest=manifest, files=files)
        notices = args.out / 'Licenses' / key
        notices.mkdir(parents=True)
        if (package / 'SOURCE-LICENSES').exists():
            shutil.copytree(package / 'SOURCE-LICENSES', notices, dirs_exist_ok=True)
        if (package / 'README.txt').exists():
            shutil.copy2(package / 'README.txt', notices / 'Package-README.txt')
    if (args.new / 'SOURCE').exists():
        shutil.copytree(args.new / 'SOURCE', args.out / 'SOURCE')
    installer_source = args.out / 'SOURCE/installer'
    installer_source.mkdir(parents=True)
    for name in ('Installer-Core.ps1', 'Setup-Manager.ps1', 'Setup-Intel.ps1', 'build-unified.py', 'UNIFIED-GUIDE.txt'):
        shutil.copy2(source / name, installer_source / name)
    (args.out / 'BUNDLE.json').write_text(json.dumps(bundle, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (args.out / 'Setup.cmd').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\npowershell.exe -NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File "%~dp0Setup-Manager.ps1"\r\n')
    shutil.copy2(source / 'UNIFIED-GUIDE.txt', args.out / 'README.txt')
    print(json.dumps(dict(bundle=str(args.out), variants={k: len(v['files']) for k, v in bundle['variants'].items()},
                          unique_files=len(list(content.iterdir())), unique_bytes=sum(p.stat().st_size for p in content.iterdir()))))


if __name__ == '__main__':
    main()
