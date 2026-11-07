"""Exercise both real offline payloads in an isolated fake game, without running a GPU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    bundle = args.bundle.resolve()
    spec = json.loads((bundle / 'BUNDLE.json').read_text(encoding='utf-8'))
    assert sha(bundle / 'Engine.ps1') == spec['engine_sha256']
    assert not (bundle / 'SOURCE/optiscaler-overlay/OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/backend/intel/nr_crash_capture.h').exists()
    root = Path(__file__).resolve().parents[2]
    scratch = root / 'work/scratch'
    ps = Path(os.environ['WINDIR']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    results = {}
    with tempfile.TemporaryDirectory(prefix='unified-complete-', dir=scratch) as tmp:
        temp = Path(tmp)
        assert temp.resolve().is_relative_to(scratch.resolve())
        game = temp / '完整 游戏目录'
        game.mkdir()
        exe = game / 'Unlisted-Game.exe'
        exe.write_bytes(b'fixture only; never executed')
        originals = {name: ('native ' + name).encode() for name in (
            'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll',
            'amd_fidelityfx_framegeneration_dx12.dll', 'nvngx_dlss.dll', 'version.dll')}
        originals['plugins/custom.asi'] = b'user ASI'
        for name, data in originals.items():
            p = game / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        env = dict(os.environ, LOCALAPPDATA=str(temp / 'profile'))
        for action, variant in [('Install', 'New'), ('Install', 'Legacy'), ('Uninstall', 'Legacy')]:
            run = subprocess.run([str(ps), '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                                  '-File', str(bundle / 'Setup-Manager.ps1'), '-BundleRoot', str(bundle),
                                  '-GameExe', str(exe), '-Action', action, '-Variant', variant,
                                  '-RemovalConfirmed'], capture_output=True, env=env)
            if run.returncode:
                logs = []
                for p in game.rglob('*.log'):
                    raw = p.read_bytes()
                    logs.append(raw.decode('utf-16' if raw[:2] == b'\xff\xfe' else 'utf-8', errors='replace'))
                raise AssertionError((run.stdout, run.stderr, logs))
            if action == 'Install':
                receipt = json.loads((game / 'intel-nr-install.json').read_text(encoding='utf-8-sig'))
                for f in receipt['files']:
                    assert sha(game / f['path']).lower() == f['sha256'].lower(), f['path']
                assert sha(game / 'dxgi.dll') == spec['variants'][variant]['host_sha256']
                assert receipt['profile'] == 'generic-dx12'
                ini = (game / 'OptiScaler.ini').read_text(encoding='utf-8-sig')
                assert 'Enabled=false' in ini and 'FGInput=upscaler' in ini
                assert (game / 'plugins/custom.asi').read_bytes() == b'user ASI'
                results[variant] = dict(verified_files=len(receipt['files']), host_sha256=sha(game / 'dxgi.dll'),
                                        unknown_game_installed=True)
            else:
                assert not (game / 'dxgi.dll').exists()
                assert not (game / 'intel-nr-install.json').exists()
                for name, data in originals.items():
                    assert (game / name).read_bytes() == data, name
                results['uninstall'] = 'PASS: native DLLs and user ASI retained/restored'
    results['real_game_or_gpu_executed'] = False
    args.out.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(results))


if __name__ == '__main__':
    main()
