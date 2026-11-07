"""Unified manager regression tests in isolated fake game directories; no GPU."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/intel-nr/Setup-Manager.ps1'
ENGINE = ROOT / 'tools/intel-nr/Setup-Intel.ps1'
PS = Path(os.environ['WINDIR']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'


def sha(data):
    return hashlib.sha256(data).hexdigest()


class UnifiedTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / 'work/scratch'
        scratch.mkdir(exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix='unified-test-', dir=scratch)
        self.root = Path(self.tmp.name)
        self.game = self.root / '游戏 空格'
        self.game.mkdir()
        self.exe = self.game / 'Unknown-Game.exe'
        self.exe.write_bytes(b'fixture only; never executed')
        self.bundle = self.root / 'bundle'
        (self.bundle / 'content').mkdir(parents=True)
        shutil.copy2(ENGINE, self.bundle / 'Engine.ps1')
        shutil.copy2(SCRIPT, self.bundle / 'Setup-Manager.ps1')
        shutil.copy2(ROOT / 'tools/intel-nr/Installer-Core.ps1', self.bundle / 'Installer-Core.ps1')
        self.spec = dict(version='test-unified', engine_sha256=sha(ENGINE.read_bytes()),
                         previous_host_sha256=[], variants={})
        for variant, marker in [('New', b'new'), ('Legacy', b'legacy')]:
            payload = {
                'OptiScaler.dll': marker + b' host',
                'OptiScaler.ini': b'[FrameGen]\nEnabled=auto\nFGInput=dlssg\nFGOutput=xefg\n[DlssNr]\nEnabled=true\nNrBackend=intel\n',
                'nrvk_bridge.dll': marker + b' runtime',
                'nr-intel/full/model.bin': b'shared model',
                'amd_fidelityfx_upscaler_dx12.dll': marker + b' SR',
            }
            rows = []
            for name, data in payload.items():
                digest = sha(data)
                (self.bundle / 'content' / digest).write_bytes(data)
                rows.append(dict(path=name, sha256=digest, bytes=len(data)))
            self.spec['variants'][variant] = dict(
                id=self.root.name + '-' + variant, host_sha256=sha(payload['OptiScaler.dll']),
                manifest=dict(version=variant, files=rows),
                files=[dict(path='payload/' + row['path'], sha256=row['sha256'], bytes=row['bytes']) for row in rows])
        self.save_spec()
        (self.game / 'amd_fidelityfx_upscaler_dx12.dll').write_bytes(b'native SR')
        (self.game / 'plugins').mkdir()
        (self.game / 'plugins/custom.asi').write_bytes(b'user plugin')

    def save_spec(self):
        (self.bundle / 'BUNDLE.json').write_text(json.dumps(self.spec), encoding='utf-8')

    def tearDown(self):
        # Only these verified isolated test directories are recursively removed.
        cache = Path(os.environ['LOCALAPPDATA']) / 'IntelNR/UnifiedPackages'
        for value in self.spec['variants'].values():
            target = cache / value['id']
            assert target.resolve().is_relative_to(cache.resolve())
            if target.exists():
                shutil.rmtree(target)
        self.tmp.cleanup()

    def run_manager(self, action='Inspect', variant='New', confirmed=True, implicit_root=False):
        command = [str(PS), '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                   '-File', str(self.bundle / 'Setup-Manager.ps1' if implicit_root else SCRIPT), '-GameExe', str(self.exe),
                   '-Action', action, '-Variant', variant]
        if not implicit_root:
            command += ['-BundleRoot', str(self.bundle)]
        if confirmed:
            command.append('-RemovalConfirmed')
        return subprocess.run(command, capture_output=True)

    def require_ok(self, *args, **kwargs):
        result = self.run_manager(*args, **kwargs)
        logs = b''
        if result.returncode:
            for path in self.game.rglob('*.log'):
                logs += path.read_bytes()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr + logs)
        return result

    def test_unknown_game_inspection_is_read_only_and_allowed(self):
        before = set(self.game.rglob('*'))
        result = self.require_ok()
        self.assertIn(b'not been tested locally', result.stdout)
        self.assertIn(b'installation is allowed', result.stdout)
        self.assertEqual(set(self.game.rglob('*')), before)
        self.require_ok('Install')
        self.assertTrue((self.game / 'intel-nr-install.json').exists())

    def test_default_bundle_root_like_double_click(self):
        before = set(self.game.rglob('*'))
        self.require_ok('Inspect', implicit_root=True)
        self.assertEqual(set(self.game.rglob('*')), before)
        self.require_ok('Install', implicit_root=True)
        self.require_ok('Uninstall', implicit_root=True)
        self.assertEqual((self.game / 'amd_fidelityfx_upscaler_dx12.dll').read_bytes(), b'native SR')

    def test_confirmation_required_before_changes(self):
        self.require_ok('Install')
        host = (self.game / 'dxgi.dll').read_bytes()
        record = (self.game / 'intel-nr-install.json').read_bytes()
        result = self.run_manager('Uninstall', confirmed=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'Continue?', result.stdout)
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), host)
        self.assertEqual((self.game / 'intel-nr-install.json').read_bytes(), record)

    def test_new_to_legacy_then_uninstall_restores_native(self):
        self.require_ok('Install')
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'new host')
        (self.game / 'OptiScaler.ini').write_bytes(b'user old settings')
        self.require_ok('Install', 'Legacy')
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'legacy host')
        self.assertNotEqual((self.game / 'OptiScaler.ini').read_bytes(), b'user old settings')
        record = json.loads((self.game / 'intel-nr-install.json').read_text(encoding='utf-8-sig'))
        self.assertEqual(record['version'], 'Legacy')
        self.require_ok('Uninstall')
        self.assertFalse((self.game / 'dxgi.dll').exists())
        self.assertFalse((self.game / 'intel-nr-install.json').exists())
        self.assertEqual((self.game / 'amd_fidelityfx_upscaler_dx12.dll').read_bytes(), b'native SR')
        self.assertEqual((self.game / 'plugins/custom.asi').read_bytes(), b'user plugin')

    def test_unmanaged_opt_is_archived_without_deleting_unknown_proxy(self):
        (self.game / 'dxgi.dll').write_bytes(b'legacy host')
        (self.game / 'version.dll').write_bytes(b'another mod')
        (self.game / 'OptiScaler.ini').write_bytes(b'old unmanaged settings')
        self.require_ok('Install')
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'new host')
        self.assertEqual((self.game / 'version.dll').read_bytes(), b'another mod')
        backups = list((self.game / 'intel-nr-backups').glob('unified-*/removed/OptiScaler.ini'))
        self.assertTrue(backups)
        self.assertEqual(backups[0].read_bytes(), b'old unmanaged settings')

    def test_legacy_private_bridge_is_archived_user_plugins_remain(self):
        (self.game / 'plugins/nr_custom.asi').write_bytes(b'another user plugin')
        (self.game / 'plugins/CyberpunkNRBridge.asi').write_bytes(b'old private NR bridge')
        (self.game / 'plugins/nr-runtime').mkdir()
        (self.game / 'plugins/nr-runtime/old-model.bin').write_bytes(b'old model')
        self.require_ok('Install')
        self.assertFalse((self.game / 'plugins/CyberpunkNRBridge.asi').exists())
        self.assertFalse((self.game / 'plugins/nr-runtime').exists())
        self.assertEqual((self.game / 'plugins/custom.asi').read_bytes(), b'user plugin')
        self.assertEqual((self.game / 'plugins/nr_custom.asi').read_bytes(), b'another user plugin')

    def test_witcher_dx11_selection_resolves_before_planning(self):
        dx11 = self.game / 'bin/x64'
        dx12 = self.game / 'bin/x64_dx12'
        dx11.mkdir(parents=True)
        dx12.mkdir()
        (dx11 / 'witcher3.exe').write_bytes(b'fixture')
        (dx12 / 'witcher3.exe').write_bytes(b'fixture')
        self.exe = dx11 / 'witcher3.exe'
        result = self.require_ok('Inspect')
        self.assertIn(b'x64_dx12', result.stdout)

    def test_corrupt_payload_stops_before_old_uninstall(self):
        self.require_ok('Install')
        old = (self.game / 'dxgi.dll').read_bytes()
        digest = self.spec['variants']['Legacy']['files'][0]['sha256']
        (self.bundle / 'content' / digest).write_bytes(b'corrupted')
        result = self.run_manager('Install', 'Legacy')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), old)

    def test_relocated_backup_preflight(self):
        self.require_ok('Install')
        path = self.game / 'intel-nr-install.json'
        record = json.loads(path.read_text(encoding='utf-8-sig'))
        record['backup'] = str(self.root / 'old-drive-missing' / Path(record['backup']).name)
        path.write_text(json.dumps(record), encoding='utf-8')
        self.require_ok('Uninstall')
        self.assertEqual((self.game / 'amd_fidelityfx_upscaler_dx12.dll').read_bytes(), b'native SR')

    def test_missing_restore_file_keeps_game_unchanged(self):
        self.require_ok('Install')
        path = self.game / 'intel-nr-install.json'
        record = json.loads(path.read_text(encoding='utf-8-sig'))
        (Path(record['backup']) / 'amd_fidelityfx_upscaler_dx12.dll').unlink()
        old = (self.game / 'dxgi.dll').read_bytes()
        result = self.run_manager('Uninstall')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), old)

    def test_failed_reinstall_restores_old_opt_state(self):
        self.require_ok('Install')
        (self.game / 'OptiScaler.ini').write_bytes(b'old preferences to restore')
        self.spec['variants']['Legacy']['manifest']['files'][0]['sha256'] = '0' * 64
        self.save_spec()
        result = self.run_manager('Install', 'Legacy')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'new host')
        self.assertEqual((self.game / 'OptiScaler.ini').read_bytes(), b'old preferences to restore')
        self.assertEqual((self.game / 'nr-intel/full/model.bin').read_bytes(), b'shared model')
        self.assertTrue((self.game / 'intel-nr-install.json').exists())


if __name__ == '__main__':
    unittest.main()
