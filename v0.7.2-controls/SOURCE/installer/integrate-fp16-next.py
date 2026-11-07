"""Stage latest Full and Small as a single payload; never deploy to games."""
import argparse,hashlib,json,shutil,zipfile
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def checked(row):
 p=Path(row['path']);assert sha(p)==row['sha256'],p;return p

def closure(root):
 manifest=root/'manifest/port_network_manifest.json';m=read(manifest);paths={manifest,root/'manifest/port_network_runtime.txt'}
 for group in ('network_shaders','bridge_shaders','cold','production_history_shaders'):
  for row in m[group].values():
   p=(manifest.parent/row['path']).resolve();assert p.is_relative_to(root.resolve());assert sha(p)==row['sha256'],p;assert p.stat().st_size==row['bytes'];paths.add(p)
 return paths

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('base','full','small','compat','out'):p.add_argument('--'+key,type=Path,required=True)
 a=p.parse_args();assert not a.out.exists();f=read(a.full/'LATEST-FULL.json');s=read(a.small/'LATEST-SMALL.json');accept=read(a.compat/'full-next-crt-acceptance.json')
 for model in (f,s):
  for row in model['production'].values():checked(row)
 assert accept['result']=='PASS' and accept['frames_bit_exact']==35
 dll=a.compat/'full-next-crt-compat/build/Release/nrvk_bridge.dll';assert sha(dll)==accept['dll_sha256']=='7fc56d5ae2bef4db9b5c27fe28939304b1ace22f7719e05f973ac74e9dc52fcd'
 archive=checked(s['archive']);identity=read(checked(s['payload_identity']));assert identity['runtime_payload_files']==152
 base=read(a.base/'PACKAGE-MANIFEST.json')
 for row in base['files']:assert sha(a.base/'payload'/row['path'])==row['sha256']
 # Fresh model directories; no obsolete weights or runtimes from previous releases.
 shutil.copytree(a.base,a.out,ignore=lambda d,n:['full','small'] if Path(d)==a.base/'payload/nr-intel' else (['vulkan'] if Path(d)==a.base/'SOURCE' else []))
 fullroot=Path(f['root'])/'model';dest=a.out/'payload/nr-intel/full';paths=closure(fullroot)
 for path in paths:
  out=dest/path.relative_to(fullroot);out.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,out)
 shutil.copy2(dll,dest/'nrvk_bridge.dll');shutil.copy2(dll,a.out/'payload/nrvk_bridge.dll')
 with zipfile.ZipFile(archive) as z:
  for row in identity['files']:
   data=z.read(row['relative']);assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256'];out=a.out/'payload'/row['relative'];assert out.resolve().is_relative_to((a.out/'payload/nr-intel/small').resolve());out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(data)
 smallpaths=closure(a.out/'payload/nr-intel/small');assert len(smallpaths)+1==152
 source=a.out/'SOURCE/FP16-Next';source.mkdir()
 shutil.copytree(a.compat/'full-next-crt-compat/source',source/'Full/source')
 shutil.copytree(fullroot/'generated',source/'Full/model/generated')
 shutil.copytree(Path(s['source']),source/'Small/source')
 shutil.copytree(a.small/'candidate/small/model/generated',source/'Small/model/generated')
 (source/'BUILD.txt').write_text('Full source: Full/source/source/vulkan. Small source: Small/source/source/vulkan.\nUse the respective model/generated and payload/nr-intel/<model>/assets and manifest.\nFull is rebuilt from sealed 62d438f6 with /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR to retain older game MSVCP140 compatibility.\nSmall retains its sealed 3b35fb14 DLL and existing mutex compatibility definition.\nFull configure: cmake -S Full/source/source/vulkan -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_GENERATOR_INSTANCE=<VS2022> -DNR_MODEL_DIR=<Full/model> -DVULKAN_INCLUDE_DIR=<Vulkan-Headers/include> "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR"\nBuild: cmake --build build --config Release --target nrvk_bridge\n',encoding='utf-8')
 proof=a.out/'VALIDATION/FP16-Next';proof.mkdir(parents=True)
 for name,folder in [('Full',a.full),('Small',a.small)]:
  d=proof/name;d.mkdir()
  for file in ('LATEST-'+name.upper()+'.json','RESULTS.json','REPORT.txt'):
   shutil.copy2(folder/file,d/file)
 shutil.copy2(a.full.parent/'FULL-TRIALS.txt',proof/'Full/FULL-TRIALS.txt')
 shutil.copy2(checked(s['payload_identity']),proof/'Small/PAYLOAD-IDENTITY.json')
 shutil.copy2(a.compat/'full-next-crt-acceptance.json',proof/'Full/CRT-COMPAT-ACCEPTANCE.json')
 shutil.copy2(a.compat/'full-next-crt-build.log',proof/'Full/CRT-COMPAT-BUILD.log')
 for file in (a.compat/'full-next-crt-compat/media-acceptance').glob('*.log'):shutil.copy2(file,proof/'Full'/file.name)
 # Current host source remains in optiscaler-overlay; model sources live in FP16-Next.
 (a.out/'SOURCE/BUILD.md').write_text('Host and Intel ASI: optiscaler-overlay and Intel-AmdSpoofing-adaptation.py.\nCurrent Full/Small: FP16-Next/BUILD.txt; both modes use this same current payload.\n',encoding='utf-8')
 shutil.copy2(Path(__file__).parent/'Setup-Intel.ps1',a.out/'Setup-Intel.ps1')
 manifest=dict(version='v0.7.0-fp16-next-offline1',files=[])
 for file in sorted((a.out/'payload').rglob('*')):
  if file.is_file():manifest['files'].append(dict(path=file.relative_to(a.out/'payload').as_posix(),bytes=file.stat().st_size,sha256=sha(file)))
 (a.out/'PACKAGE-MANIFEST.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
 note='最新 Full、Small 已集成，同一套宿主与模型供两种前置挂载方式使用；不提供整套旧版本。\nLatest Full and Small integrated; both attachment modes share the same current host and models. No complete legacy version.\nSmall 保持封存 3b35fb14；Full 基于封存 62d438f6 补回旧 CRT 兼容构建选项，交付哈希 7fc56d5a。\nSmall retains sealed 3b35fb14. Full is rebuilt from sealed 62d438f6 with the older CRT compatibility option; delivered hash 7fc56d5a.\nFull 兼容构建五组 35 帧输出与封存版逐位一致；该构建未重新测速。\nThe Full compatibility build passed five media cases, all 35 frames bit-exact to sealed Full; performance was not remeasured for this build.\n封存 Full 含 history 均值节省 0.0337 ms，标准区段慢 0.0358 ms；Small 含 history 节省 1.3895 ms。\nSealed Full saves 0.0337 ms including history while its standard interval is 0.0358 ms slower; Small saves 1.3895 ms including history.\n最新模型尚未换装游戏，游戏内效果待复验。既有三款游戏测试结果指宿主与通道的先前模型配置。\nLatest models have not been deployed to games; in-game verification is pending. Prior three-game results cover the host/routing with earlier model builds.\n'
 (a.out/'MODEL-STATUS.txt').write_text(note,encoding='utf-8');(a.out/'README.txt').write_text(note,encoding='utf-8')
 print(json.dumps(dict(package=str(a.out),files=len(manifest['files']),full_runtime_files=len(paths)+1,small_runtime_files=152,full_sha256=sha(dll),small_sha256=identity['dll_sha256'])))
if __name__=='__main__':main()
