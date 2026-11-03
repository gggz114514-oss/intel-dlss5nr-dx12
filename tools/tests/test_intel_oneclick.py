"""Windows PowerShell 5.1 installer smoke, isolated files, no GPU or games."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SCRIPT=ROOT/'tools/intel-nr/Setup-Intel.ps1'
PS=Path(os.environ['WINDIR'])/'System32/WindowsPowerShell/v1.0/powershell.exe'

class InstallerTests(unittest.TestCase):
 def setUp(self):
  scratch=ROOT/'work/scratch'; scratch.mkdir(parents=True,exist_ok=True)
  self.tmp=tempfile.TemporaryDirectory(prefix='intel-user-install-',dir=scratch)
  self.root=Path(self.tmp.name);self.game=self.root/'游戏目录';self.game.mkdir()
  self.package=self.root/'package';self.payload=self.package/'payload';self.payload.mkdir(parents=True)
  (self.game/'ds.exe').write_bytes(b'fixture, not executable')
  (self.game/'winhttp.dll').write_bytes(b'original')
  (self.game/'OptiScaler.ini').write_bytes(b'user preferences')
  for name,data in [('OptiScaler.dll',b'new'),('OptiScaler.ini',b'defaults'),('nr_hdr_proxy.hlsl',b'shader')]:
   (self.payload/name).write_bytes(data)
  files=[dict(path=f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in self.payload.iterdir()]
  (self.package/'PACKAGE-MANIFEST.json').write_text(json.dumps(dict(version='test',files=files)))
 def tearDown(self):self.tmp.cleanup()
 def run_script(self,*args):
  return subprocess.run([str(PS),'-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',str(SCRIPT),'-GameDir',str(self.game/'ds.exe'),'-PackageDir',str(self.package),*args],capture_output=True)
 def test_install_preserve_uninstall_and_repeat(self):
  p=self.run_script();self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertEqual((self.game/'OptiScaler.ini').read_bytes(),b'user preferences')
  self.assertEqual((self.game/'winhttp.dll').read_bytes(),b'new')
  p=self.run_script('-CollectLogs');self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  p=self.run_script();self.assertNotEqual(p.returncode,0)
  p=self.run_script('-Uninstall');self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertEqual((self.game/'winhttp.dll').read_bytes(),b'original')
  self.assertFalse((self.game/'nr_hdr_proxy.hlsl').exists())
  self.assertFalse((self.game/'intel-nr-install.json').exists())
 def test_corrupt_payload_changes_nothing(self):
  (self.payload/'OptiScaler.dll').write_bytes(b'corrupted')
  p=self.run_script();self.assertNotEqual(p.returncode,0)
  self.assertEqual((self.game/'winhttp.dll').read_bytes(),b'original')
  self.assertFalse((self.game/'intel-nr-install.json').exists())
 def test_package_directory_default_like_double_click(self):
  script=self.package/'Setup-Intel.ps1';shutil.copy2(SCRIPT,script)
  for args in ([],['-Uninstall']):
   p=subprocess.run([str(PS),'-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',str(script),'-GameDir',str(self.game),*args],capture_output=True,cwd=self.root)
   self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertEqual((self.game/'winhttp.dll').read_bytes(),b'original')
 def test_native_fidelityfx_dependencies_restored(self):
  names=['amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll','amd_fidelityfx_upscaler_dx12.dll']
  manifest=json.loads((self.package/'PACKAGE-MANIFEST.json').read_text())
  for name in names:
   (self.game/name).write_bytes(b'game native '+name.encode())
   data=b'package version '+name.encode();(self.payload/name).write_bytes(data)
   manifest['files'].append(dict(path=name,sha256=hashlib.sha256(data).hexdigest()))
  (self.package/'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest))
  for args in ([],['-Uninstall']):
   p=self.run_script(*args);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  for name in names:self.assertEqual((self.game/name).read_bytes(),b'game native '+name.encode())
 def test_death_stranding_clean_fg_input(self):
  (self.game/'OptiScaler.ini').unlink()
  ini=b'[FrameGen]\nEnabled=true\nFGInput=dlssg\nFGOutput=xefg\n[Spoofing]\nDxgi=true\nStreamlineSpoofing=true\n'
  (self.payload/'OptiScaler.ini').write_bytes(ini)
  manifest=json.loads((self.package/'PACKAGE-MANIFEST.json').read_text())
  for f in manifest['files']:
   if f['path']=='OptiScaler.ini':f['sha256']=hashlib.sha256(ini).hexdigest()
  (self.package/'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest))
  p=self.run_script();self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertEqual((self.game/'OptiScaler.ini').read_bytes(),ini.replace(b'FGInput=dlssg',b'FGInput=upscaler'))

 def test_generic_exe_install_and_folder_uninstall(self):
  (self.game/'ds.exe').rename(self.game/'OtherGame-Win64-Shipping.exe')
  (self.game/'winhttp.dll').unlink()
  (self.game/'OptiScaler.ini').unlink()
  ini=b'[FrameGen]\nEnabled=true\nFGInput=dlssg\nFGOutput=xefg\n'
  (self.payload/'OptiScaler.ini').write_bytes(ini)
  manifest=json.loads((self.package/'PACKAGE-MANIFEST.json').read_text())
  for f in manifest['files']:
   if f['path']=='OptiScaler.ini':f['sha256']=hashlib.sha256(ini).hexdigest()
  (self.package/'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest))
  for target,args in [(self.game/'OtherGame-Win64-Shipping.exe',[]),(self.game,['-CollectLogs']),(self.game,['-Uninstall'])]:
   p=subprocess.run([str(PS),'-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',str(SCRIPT),'-GameDir',str(target),'-PackageDir',str(self.package),*args],capture_output=True)
   self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   if not args:
    self.assertEqual((self.game/'dxgi.dll').read_bytes(),b'new')
    self.assertIn(b'Enabled=false',(self.game/'OptiScaler.ini').read_bytes())
    self.assertIn(b'FGInput=upscaler',(self.game/'OptiScaler.ini').read_bytes())
    self.assertEqual(json.loads((self.game/'intel-nr-install.json').read_text(encoding='utf-8-sig'))['profile'],'generic-dx12')
  self.assertFalse((self.game/'dxgi.dll').exists())

if __name__=='__main__':unittest.main()
