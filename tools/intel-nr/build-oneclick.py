"""Assemble an end-user Intel NR + FSR XMX installer from a complete local payload."""
import argparse, hashlib, json, pathlib, shutil, zipfile

p=argparse.ArgumentParser()
p.add_argument('--base-package', help='Repackage an existing complete payload without a game installation')
p.add_argument('--known-install')
p.add_argument('--host')
p.add_argument('--shader')
p.add_argument('--xmx')
p.add_argument('--opt-package')
p.add_argument('--out',required=True)
a=p.parse_args()
out=pathlib.Path(a.out)
if out.exists(): raise SystemExit('Use a new output directory.')
payload=out/'payload'
if a.base_package:
 shutil.copytree(pathlib.Path(a.base_package)/'payload',payload)
 if a.host: shutil.copy2(a.host,payload/'OptiScaler.dll')
 if a.shader: shutil.copy2(a.shader,payload/'nr_hdr_proxy.hlsl')
else:
 if not all((a.known_install,a.host,a.shader,a.xmx,a.opt_package)):
  p.error('Provide --base-package or all source options.')
 src=pathlib.Path(a.known_install)
 payload.mkdir(parents=True)
 def copy(source,relative):
  dest=payload/relative; dest.parent.mkdir(parents=True,exist_ok=True)
  shutil.copy2(source,dest)
 copy(a.host,'OptiScaler.dll'); copy(a.shader,'nr_hdr_proxy.hlsl')
 copy(src/'nrvk_bridge.dll','nrvk_bridge.dll')
 for model in ('full','small'):
  for folder in ('assets','manifest'):
   shutil.copytree(src/'nr-intel'/model/folder,payload/'nr-intel'/model/folder)
  copy(src/'nr-intel'/model/'nrvk_bridge.dll',f'nr-intel/{model}/nrvk_bridge.dll')
 for name in ('amd_fidelityfx_upscaler_dx12.dll','fsr-original.dll'):
  copy(pathlib.Path(a.xmx)/name,name)
 # Use the installed, working local dependency set; never copy game-specific DLLs.
 deps=('amd_fidelityfx_dx12.dll','amd_fidelityfx_framegeneration_dx12.dll',
  'd3dcompiler_47.dll','dxcompiler.dll','dxil.dll','fakenvapi.dll','fakenvapi.ini',
  'libxell.dll','libxess.dll','libxess_fg.dll','libxess_dx11.dll','igxess.dll',
  'dlssg_to_fsr3_amd_is_better.dll','XeFX.dll','XeFX_Loader.dll',
  'D3D12_Optiscaler/D3D12Core.dll')
 for name in deps: copy(src/name,name)
 with zipfile.ZipFile(a.opt_package) as z:
  for name in ('amd_fidelityfx_denoiser_dx12.dll','amd_fidelityfx_loader_dx12.dll'):
   (payload/name).write_bytes(z.read('OptiScaler/'+name))
  for info in z.infolist():
   if info.filename.startswith('Licenses/') and not info.is_dir():
    dst=payload/info.filename;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(z.read(info))
(payload/'OptiScaler.ini').write_text('''[Upscalers]
Dx12Upscaler=ffx
[FSR]
UpscalerIndex=auto
[FrameGen]
Enabled=true
FGInput=dlssg
FGOutput=xefg
[Spoofing]
Dxgi=true
StreamlineSpoofing=true
SpoofedVendorId=0x10de
SpoofedDeviceId=0x2684
SpoofedGPUName=NVIDIA GeForce RTX 4090
[DlssNr]
Enabled=true
RunBeforeSR=true
NrBackend=intel
IntelNetworkModel=0
IntelInputHeight=0
IntelStyle=0
IntelTone=1
IntelDisplayStrength=1
[Log]
LogToFile=true
LogLevel=1
LogFileName=OptiScaler.log
''',encoding='utf-8')
installer=pathlib.Path(__file__).with_name('Setup-Intel.ps1')
(out/'Setup-Intel.ps1').write_text(installer.read_text(encoding='utf-8-sig'),encoding='utf-8-sig')
for name,extra in [('Install.cmd',''),('Uninstall.cmd',' -Uninstall'),('Collect-Logs.cmd',' -CollectLogs')]:
 (out/name).write_bytes(('@echo off\r\ncd /d "%~dp0"\r\npowershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Setup-Intel.ps1"'+extra+' %*\r\nif errorlevel 1 (echo FAILED) else (echo DONE)\r\npause\r\n').encode('ascii'))
files=[]
for f in sorted(payload.rglob('*')):
 if f.is_file():files.append(dict(path=f.relative_to(payload).as_posix(),bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest()))
(out/'PACKAGE-MANIFEST.json').write_text(json.dumps(dict(version='Intel-NR-20261103-r3-natural-fix-small-align4k-nvidia-xefg',files=files),indent=2),encoding='utf-8')
shutil.copy2(pathlib.Path(__file__).with_name('USER-GUIDE.txt'),out/'README.txt')
print(json.dumps(dict(package=str(out),files=len(files),bytes=sum(f['bytes'] for f in files))))
