"""Verify a real complete package, then install/uninstall in an isolated fixture (no GPU)."""
import argparse
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--package',type=Path,required=True)
 parser.add_argument('--out',type=Path,required=True)
 args=parser.parse_args()
 package=args.package.resolve();payload=package/'payload'
 manifest=json.loads((package/'PACKAGE-MANIFEST.json').read_text(encoding='utf-8'))
 for f in manifest['files']:
  assert sha(payload/f['path'])==f['sha256'],f['path']
 # Pin the mutex-compatible runtimes, accepted Intel ASI, and unchanged input shader.
 expected={
  'nr-intel/full/nrvk_bridge.dll':'ef4eb02954432bb190c19f8c65b11ebb7f327ad84e97876d95e95a58668148a6',
  'nr-intel/small/nrvk_bridge.dll':'f68ee7367454eb7328c50c268f56f81ecf0cd8e74f9956d8ef1d599f5acd2e0c',
  'nr_hdr_proxy.hlsl':'2e86a602957fae56236d57cccdd45dd0276882dd7da2cf65d6f6926b5fe1cfc1',
  'amd_fidelityfx_upscaler_dx12.dll':'a609f609a3ef29ddff59adfbd27dc417bf68fcd865383b2529987a7eff1081ac',
  'plugins/AmdSpoofing.asi':'3edba77b3e7a9bc650765d492ef72b9515b6cd540f9092b962e934bc7fff7a92',
  'nvngx_dlss.dll':'d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e',
 }
 if 'fp16-next' in manifest['version']:
  expected['nr-intel/full/nrvk_bridge.dll']='7fc56d5ae2bef4db9b5c27fe28939304b1ace22f7719e05f973ac74e9dc52fcd'
  expected['nr-intel/small/nrvk_bridge.dll']='3b35fb14aff399158d86dad04ca414a1c27ed89b09f003a0307ae1b27a7bd9b9'
 elif 'small-color' in manifest['version']:
  expected['nr-intel/small/nrvk_bridge.dll']='5abcb92151062085b2b750791d2aadebc4cf9edec9211be356e5bab2994e7987'
 for name,digest in expected.items(): assert sha(payload/name)==digest,name
 for name in ('dlssnr_amd_pass1.dll','LmxxfNrRuntime.dll','MochizukiNrRuntime.dll'):
  assert not (payload/name).exists(),name
 # Query the actual DLL with its adjacent model files. No D3D device is created.
 class Header(c.Structure):
  _fields_=[('type',c.c_uint64),('next',c.c_void_p)]
 class Versions(c.Structure):
  _fields_=[('header',Header),('effect',c.c_uint64),('device',c.c_void_p),('count',c.POINTER(c.c_uint64)),('ids',c.POINTER(c.c_uint64)),('names',c.POINTER(c.c_char_p))]
 os.environ.pop('FSR4_XMX_DLSS_DLL',None)
 dll=c.WinDLL(str(payload/'amd_fidelityfx_upscaler_dx12.dll'))
 dll.ffxQuery.argtypes=[c.c_void_p,c.c_void_p];dll.ffxQuery.restype=c.c_uint32
 count=c.c_uint64(0)
 query=Versions(Header(4,None),0x10000,None,c.pointer(count),None,None)
 assert dll.ffxQuery(None,c.byref(query))==0
 assert count.value==27,count.value
 ids=(c.c_uint64*count.value)();names=(c.c_char_p*count.value)();query.ids=ids;query.names=names
 assert dll.ffxQuery(None,c.byref(query))==0
 providers=[names[i].decode() for i in range(count.value)]
 for name in ('4.0.2X','4.1.2X','DLSS-EX','DLSS-FX','DLSS-EFX','DLSS-KX','DLSS-LX','DLSS-MX'):assert name in providers,name
 root=Path(__file__).resolve().parents[2]
 scratch=root/'work/scratch';scratch.mkdir(parents=True,exist_ok=True)
 ps=Path(os.environ['WINDIR'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
 with tempfile.TemporaryDirectory(prefix='complete-package-',dir=scratch) as tmp:
  game=Path(tmp)/'游戏';game.mkdir()
  # TemporaryDirectory only removes the verified isolated fixture under work/scratch.
  assert game.resolve().is_relative_to(scratch.resolve())
  (game/'Generic-Win64-Shipping.exe').write_bytes(b'fixture: never executed')
  originals={name:('native '+name).encode() for name in ('dxgi.dll','version.dll','nvngx_dlss.dll','amd_fidelityfx_loader_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll','amd_fidelityfx_upscaler_dx12.dll')}
  originals['OptiScaler.ini']=b'[Upscalers]\nDx12Upscaler=ffx\n[FSR]\nUpscalerIndex=3\n[DlssNr]\nEnabled=true\nNrBackend=off\nIntelNetworkModel=1\n[Plugins]\nPath=my-plugins\n'
  originals['my-plugins/AmdSpoofing.asi']=b'previous ASI'
  originals['my-plugins/custom.asi']=b'user ASI'
  for name,data in originals.items():
   dest=game/name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
  def run(*extra):
   result=subprocess.run([str(ps),'-NoProfile','-NonInteractive','-STA','-ExecutionPolicy','Bypass','-File',str(package/'Setup-Intel.ps1'),'-GameDir',str(game/'Generic-Win64-Shipping.exe'),'-PackageDir',str(package),*extra],capture_output=True)
   assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
  run()
  receipt=json.loads((game/'intel-nr-install.json').read_text(encoding='utf-8-sig'))
  assert receipt['profile']=='generic-dx12'
  assert len(receipt['files'])==len(manifest['files'])
  ini=(game/'OptiScaler.ini').read_text(encoding='utf-8')
  for value in ('NrBackend=intel','Enabled=false','IntelNetworkModel=1','UpscalerIndex=3','Path=my-plugins'):assert value in ini,value
  assert sha(game/'my-plugins/AmdSpoofing.asi')==expected['plugins/AmdSpoofing.asi']
  assert sha(game/'dxgi.dll')==sha(payload/'OptiScaler.dll')
  (game/'my-plugins/added.asi').write_bytes(b'added after installation')
  run('-Uninstall')
  for name,data in originals.items():assert (game/name).read_bytes()==data,name
  assert (game/'my-plugins/added.asi').read_bytes()==b'added after installation'
  assert not (game/'nr-intel/small/nrvk_bridge.dll').exists()
  assert not (game/'intel-nr-install.json').exists()
 result={'payload_files':len(manifest['files']),'payload_hashes':'PASS','stable_nr_and_shader_hashes':'PASS','ffx_providers':providers,'complete_install_uninstall':'PASS','legacy_off_and_preferences':'PASS','native_dlls_and_plugins_restored':'PASS','new_user_plugin_retained':'PASS','gpu_or_game_tested':False}
 args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(result))

if __name__=='__main__': main()
