"""Apply the Intel host overlay to the exact upstream source snapshot (no build)."""
from pathlib import Path
import argparse, hashlib, json, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--upstream', type=Path, required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
overlay=root/'source/optiscaler-overlay'
meta=json.loads((overlay/'UPSTREAM.json').read_text())
# A fresh clone pins the submodule graph as well as host source.
head=subprocess.check_output(['git','-C',str(a.upstream),'rev-parse','HEAD'],text=True).strip()
if head != meta['commit']: raise SystemExit('Checkout upstream commit '+meta['commit']+' first.')
for row in meta['files']:
 src=overlay/row['path']; dst=a.upstream/row['path']
 if hashlib.sha256(src.read_bytes()).hexdigest()!=row['sha256']: raise SystemExit('Overlay hash mismatch: '+row['path'])
 current=hashlib.sha256(dst.read_bytes()).hexdigest() if dst.exists() else None
 if current not in (row['upstream_sha256'],row['sha256']): raise SystemExit('Local edit would be overwritten: '+row['path'])
for row in meta['files']:
 dst=a.upstream/row['path'];dst.parent.mkdir(parents=True,exist_ok=True)
 dst.write_bytes((overlay/row['path']).read_bytes())
print('Applied',len(meta['files']),'host files to',meta['commit'])
