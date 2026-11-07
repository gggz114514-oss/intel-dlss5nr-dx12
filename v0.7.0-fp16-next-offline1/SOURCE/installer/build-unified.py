"""Build the bilingual offline manager with one current payload and two attachment/spoofing modes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--version', default='v0.7.0-fp16-next-offline1')
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
    bundle = dict(version=args.version, engine_sha256=sha(args.out / 'Engine.ps1'),
                  previous_host_sha256=[
                      'b1e1bff70ad4251917e6e8cbf3aeb9ec6dcf7d25d4ae0bffbd53cbb300f0ca1c',
                      'ab731be6ac1d63ee0d07cf404c21049dce0ad4cc7b6415f169746e7f92a33483'],
                  previous_spoof_sha256=[])
    for package in (args.package,):
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
        bundle['payload'] = dict(id='current-' + sha(package / 'PACKAGE-MANIFEST.json')[:16],
                                      host_sha256=sha(package / 'payload/OptiScaler.dll'),
                                      manifest=manifest, files=files)
        bundle['spoof_sha256'] = sha(package / 'payload/plugins/AmdSpoofing.asi')
        notices = args.out / 'Licenses'
        notices.mkdir(parents=True)
        if (package / 'SOURCE-LICENSES').exists():
            shutil.copytree(package / 'SOURCE-LICENSES', notices, dirs_exist_ok=True)
        if (package / 'README.txt').exists():
            shutil.copy2(package / 'README.txt', notices / 'Package-README.txt')
    if (args.package / 'SOURCE').exists():
        shutil.copytree(args.package / 'SOURCE', args.out / 'SOURCE')
    if (args.package / 'VALIDATION').exists():
        shutil.copytree(args.package / 'VALIDATION', args.out / 'VALIDATION')
    if (args.package / 'MODEL-STATUS.txt').exists():
        shutil.copy2(args.package / 'MODEL-STATUS.txt', args.out / 'MODEL-STATUS.txt')
    installer_source = args.out / 'SOURCE/installer'
    installer_source.mkdir(parents=True)
    for name in ('Installer-Core.ps1', 'Setup-Manager.ps1', 'Setup-Intel.ps1', 'build-unified.py', 'integrate-fp16-next.py', 'UNIFIED-GUIDE.txt'):
        shutil.copy2(source / name, installer_source / name)
    (args.out / 'BUNDLE.json').write_text(json.dumps(bundle, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (args.out / 'Setup.cmd').write_bytes(b'@echo off\r\ncd /d "%~dp0"\r\npowershell.exe -NoProfile -STA -WindowStyle Hidden -ExecutionPolicy Bypass -File "%~dp0Setup-Manager.ps1"\r\n')
    shutil.copy2(source / 'UNIFIED-GUIDE.txt', args.out / 'README.txt')
    if (args.package / 'MODEL-STATUS.txt').exists():
        with (args.out / 'README.txt').open('a', encoding='utf-8') as f:
            f.write('\n' + (args.package / 'MODEL-STATUS.txt').read_text(encoding='utf-8'))
    print(json.dumps(dict(bundle=str(args.out), payload_files=len(bundle['payload']['files']),
                          unique_files=len(list(content.iterdir())), unique_bytes=sum(p.stat().st_size for p in content.iterdir()))))


if __name__ == '__main__':
    main()
