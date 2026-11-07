"""Retarget the exact Overclockers 0.0.1 ASI's source-adapter filters to Intel.

This is a binary adaptation of that plugin, not a replacement implementation.
The original hook bodies, identity-preservation guard and export ABI are retained.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ORIGINAL_SHA256='293c47c7ef98d358fe09156d50a105c886e5146f6684fcf0c5761d8471e6e053'
# Reviewed instructions: ANSI/UTF16 VEN_/DEV_ parsing and rewriting (first six),
# SetupDi numeric property rewriting, configured source-vendor admission, and
# D3DKMT adapter-ID rewriting. Only the physical source vendor changes.
PATCHES=(
 (0x1258,'bf02100000'),(0x1750,'b802100000'),(0x17ce,'bf02100000'),
 (0x18bd,'be02100000'),(0x1dfe,'b802100000'),(0x1e7f,'be02100000'),
 (0x8e27,'3d02100000'),(0xb67a,'b802100000'),(0xe51c,'817b0402100000'),
)
# Physical-adapter discovery builds XOR-0x64 UTF16 "VEN_1002&DEV_" on
# the stack, then matches registry MatchingDeviceId. Retarget both digit pairs.
ENCODED_PATCHES=(
 (0x60c5,'c7458055005400','c745805c005400'),
 (0x60cc,'c7458454005600','c745845c005200'),
)

def adapt(source,output,record):
 original=source.read_bytes()
 if hashlib.sha256(original).hexdigest()!=ORIGINAL_SHA256:
  raise ValueError('Requires the original Overclockers amd_spoofing_enhance_0.0.1 asset.')
 data=bytearray(original)
 pe=struct.unpack_from('<I',data,0x3c)[0]
 count=struct.unpack_from('<H',data,pe+6)[0]
 sections=pe+24+struct.unpack_from('<H',data,pe+20)[0]
 def offset(rva):
  for i in range(count):
   size,base,rawsize,raw=struct.unpack_from('<IIII',data,sections+i*40+8)
   if base<=rva<base+rawsize:return raw+rva-base
  raise ValueError('RVA is not file-backed')
 edits=[]
 for rva,hexcode in PATCHES:
  before=bytes.fromhex(hexcode);after=before.replace(b'\x02\x10\x00\x00',b'\x86\x80\x00\x00')
  start=offset(rva)
  assert data[start:start+len(before)]==before,hex(rva)
  data[start:start+len(before)]=after
  edits.append(dict(rva=hex(rva),offset=hex(start),before=before.hex(),after=after.hex()))
 for rva,before_hex,after_hex in ENCODED_PATCHES:
  start=offset(rva);before=bytes.fromhex(before_hex);after=bytes.fromhex(after_hex)
  assert data[start:start+len(before)]==before,hex(rva)
  data[start:start+len(before)]=after
  edits.append(dict(rva=hex(rva),offset=hex(start),before=before.hex(),after=after.hex()))
 before=b'inactive: no matching AMD display adapter'
 after=b'inactive: no matching Intel adapter'
 assert data.count(before)==1
 start=data.index(before);data[start:start+len(before)]=after.ljust(len(before),b'\0')
 edits.append(dict(offset=hex(start),before=before.hex(),after=after.ljust(len(before),b'\0').hex()))
 assert len(data)==len(original)
 output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(data)
 result=dict(source='https://github.com/Overclockers/OptiScaler-Releases/releases/tag/amd_spoofing_enhance_0.0.1',
  adaptation='Intel-only source vendor 0x8086; original hook implementation retained',
  original_sha256=ORIGINAL_SHA256,sha256=hashlib.sha256(data).hexdigest(),edits=edits,
  license='No independent license supplied by original release; no relicensing asserted.')
 record.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({k:v for k,v in result.items() if k!='edits'}))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--original',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--record',type=Path,required=True);a=p.parse_args()
 adapt(a.original,a.out,a.record)
