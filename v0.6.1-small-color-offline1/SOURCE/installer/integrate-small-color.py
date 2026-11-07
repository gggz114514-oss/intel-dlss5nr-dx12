"""Stage the exact offline-accepted Small component; never deploy it to a game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Use a new output directory.')
    results = json.loads((args.validation / 'RESULTS.json').read_text())
    archive = args.validation / 'Small-FP16-color-fix-20261105.zip'
    assert sha(archive.read_bytes()) == results['integration_archive']['sha256']
    for key in ('component', 'cpu_ctest', 'integration_identity', 'source_patch'):
        row = results[key]
        assert sha(Path(row['path']).read_bytes()) == row['sha256'], key
    base_manifest = json.loads((args.base / 'PACKAGE-MANIFEST.json').read_text())
    for item in base_manifest['files']:
        assert sha((args.base / 'payload' / item['path']).read_bytes()) == item['sha256']
    # Use a fresh tree so obsolete Small weight names cannot survive an overlay.
    small_parent = args.base / 'payload/nr-intel'
    shutil.copytree(args.base, args.out,
                    ignore=lambda directory, names: ['small'] if Path(directory) == small_parent else [])
    with zipfile.ZipFile(archive) as z:
        identity = json.loads(z.read('INTEGRATION.json'))
        expected = {row['path']: row for row in identity['files']}
        actual = {name for name in z.namelist() if name.startswith('nr-intel/small/') and not name.endswith('/')}
        assert actual == set(expected)
        for name, row in expected.items():
            data = z.read(name)
            assert len(data) == row['bytes'] and sha(data) == row['sha256'], name
            dest = args.out / 'payload' / name
            assert dest.resolve().is_relative_to((args.out / 'payload/nr-intel/small').resolve())
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
    manifest = dict(version='v0.5.1-dx12-small-color-offline1', files=[])
    for path in sorted((args.out / 'payload').rglob('*')):
        if path.is_file():
            manifest['files'].append(dict(path=path.relative_to(args.out / 'payload').as_posix(),
                                          bytes=path.stat().st_size, sha256=sha(path.read_bytes())))
    (args.out / 'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest, indent=2) + '\n')
    proof = args.out / 'VALIDATION/Small-Color-Fix'
    proof.mkdir(parents=True)
    for name in ('RESULTS.json', 'INTEGRATION-IDENTITY.json', 'FINAL-DLL-MEDIA-REVIEW.json', 'COMPLETE-SOURCE-FIX-V3.patch'):
        shutil.copy2(args.validation / name, proof / name)
    source = args.out / 'SOURCE/Small-Color-Fix'
    bridge = args.validation.parent / 'bridge-source'
    shutil.copytree(bridge / 'source/vulkan', source / 'bridge-source/source/vulkan')
    shutil.copytree(bridge / 'source/bridge/include', source / 'bridge-source/source/bridge/include')
    shutil.copytree(args.validation / 'repair-v3/model/generated', source / 'model/generated')
    for row in results['source_changed_files']:
        path = Path(row['after']['path'])
        assert sha(path.read_bytes()) == row['after']['sha256']
        dest = source / 'source-fix' / row['relative']
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, dest)
    notice = ('新版 Small 色彩实现修复已通过交接记录中的离线数值、实际 DLL 与五组媒体验收；游戏内观感尚待复验，未换装游戏。\n'
              'New Small color implementation fix passed the supplied offline numerical, actual-DLL and five-media acceptance; in-game visuals remain unverified. No game deployment.\n'
              'Full、宿主和补帧通道保持已实测版本；旧版回退方案保留旧 Small。\n'
              'Full, the host and FG routing retain their tested versions; the legacy rollback variant keeps its earlier Small.\n')
    (args.out / 'SMALL-COLOR-STATUS.txt').write_text(notice, encoding='utf-8')
    with (args.out / 'README.txt').open('a', encoding='utf-8') as f:
        f.write('\n' + notice)
    # Candidate DLL has a frozen bridge snapshot; include that exact snapshot,
    # not an assertion that the shared Full bridge source is byte-identical.
    (source / 'BUILD.txt').write_text(
        'Exact tested Small bridge: bridge-source/source/vulkan, model/generated.\n'
        'Use payload/nr-intel/small/assets and manifest with these generated headers.\n'
        'CMake defines _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR. The snapshot retains the tested 4 KiB pool rule.\n'
        'The shared SOURCE/vulkan remains the Full build source; use this frozen snapshot to reproduce the accepted Small DLL.\n', encoding='utf-8')
    print(json.dumps(dict(package=str(args.out), files=len(manifest['files']), small_files=len(expected),
                          small_dll_sha256=identity['candidate_dll_sha256'], game_acceptance='pending')))


if __name__ == '__main__':
    main()
