"""Rebuild the two production-history variants from their frozen ordinary SPV.

This repeats the final Full pointer-root transformation without compiling any
floating expression. F01 old=6; F21 old=8,next=9. Whole-buffer typed offsets
remain push[25]/push[29], supplied by the production bridge.
"""
from pathlib import Path
import json,struct,sys
sys.dont_write_bytecode=True

def ops(data):
 w=list(struct.unpack('<%dI'%(len(data)//4),data));oo=[];at=5
 while at<len(w):n=w[at]>>16;assert n;oo.append(w[at:at+n]);at+=n
 return w[:5],oo
def op(code,*args):return [((len(args)+1)<<16)|code,*args]
def string(words):return struct.pack('<%dI'%len(words),*words).split(b'\0',1)[0].decode()
def make_variant(source,output,prefix):
 source,output=Path(source),Path(output);h,oo=ops(source.read_bytes())
 names={o[1]:string(o[2:]) for o in oo if o[0]&65535==5}
 rgba=next(k for k,n in names.items() if n.startswith('nrf_rgba_clamped(' if prefix=='f01' else 'rgba('))
 linear=next(k for k,n in names.items() if n.startswith('nrf_hist_linear(' if prefix=='f01' else 'hist_linear('))
 f=next(o[1] for o in oo if o[0]&65535==71 and o[2:]==[33,2 if prefix=='f01' else 1])
 funcs={};current=None
 for o in oo:
  code=o[0]&65535
  if code==54:current=o[2];funcs[current]=[]
  if current is not None:funcs[current].append(o)
  if code==56:current=None
 helper=funcs[rgba]
 assert all(o[0]&65535 in [54,55,56,57,59,61,62,65,80,124,128,132,248,254] for o in helper)
 result={o[2] for o in helper if o[0]&65535 not in [56,62,248,254]}|{o[1] for o in helper if o[0]&65535==248}
 assert min(result)>7
 mapping={v:h[3]+i for i,v in enumerate(sorted(result))};newid=h[3]+len(mapping);oldvar=newid;newid+=1
 clone=[[o[0],*[mapping.get(v,v) for v in o[1:]]] for o in helper];loads=0
 for o in clone:
  if o[0]&65535==65 and o[3]==f:o[3]=oldvar;loads+=1
 assert loads==4
 var=next(o for o in oo if o[0]&65535==59 and o[2]==f)
 glob=[op(59,var[1],oldvar,var[3])];decor=[op(71,oldvar,34,0),op(71,oldvar,33,6 if prefix=='f01' else 8)]
 if prefix=='f21':
  surface=next(o[2] for o in oo if o[0]&65535==59 and names.get(o[2],'')=='' and any(d[0]&65535==71 and d[1]==o[2] and d[2:]==[33,7] for d in oo))
  sv=next(o for o in oo if o[0]&65535==59 and o[2]==surface);nextvar=newid;newid+=1
  glob.append(op(59,sv[1],nextvar,sv[3]));decor.extend([op(71,nextvar,34,0),op(71,nextvar,33,9)])
  slot=next(k for k,n in names.items() if n=='history_slot');slotloads={o[2] for o in oo if o[0]&65535==61 and o[3]==slot}
 current=None;calls=writes=0;changes=[]
 original=[o.copy() for o in oo]
 for at,o in enumerate(oo):
  code=o[0]&65535
  if code==54:current=o[2]
  if current==linear and code==57 and o[3]==rgba:
   before=o.copy();o[3]=mapping[rgba];calls+=1;changes.append(dict(at=at,before=before,after=o.copy()))
  if prefix=='f21' and code==65 and o[3]==surface and o[-1] in slotloads:
   before=o.copy();o[3]=nextvar;writes+=1;changes.append(dict(at=at,before=before,after=o.copy()))
  if code==15:o.extend([oldvar]+([nextvar] if prefix=='f21' else []));o[0]=(len(o)<<16)|15
  if code==56:current=None
 assert calls==4 and writes==(prefix=='f21')
 t=next(i for i,o in enumerate(oo) if o[0]&65535==19);oo[t:t]=decor
 t=next(i for i,o in enumerate(oo) if o[0]&65535==54);oo[t:t]=glob
 oo.extend(clone);h[3]=newid;words=h+[v for o in oo for v in o]
 output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(struct.pack('<%dI'%len(words),*words))
 nc=lambda x:[o for o in x if o[0]&65535==71 and len(o)==3 and o[2]==42]
 fp=lambda x:[o for o in x if o[0]&65535 in [12,129,131,133,136,140,141,142,143,144,145,146,147,148]]
 # The helper clone contains only integer address ops, composite construction,
 # loads/stores and calls to mirror. It adds no floating arithmetic.
 assert nc(original)==nc(oo) and fp(original)==fp(oo)
 return dict(prefix=prefix,old_binding=6 if prefix=='f01' else 8,next_binding=9 if prefix=='f21' else None,
    inherited_floating_instructions_exact=True,inherited_NC_exact=True,helper_reads=loads,
    history_calls=calls,history_write_roots=writes,changes=changes,
    cloned_helper_exact_modulo_local_ids_and_four_roots=True)

if __name__=='__main__':
 assert len(sys.argv)==4,'history_variants.py input.spv output.spv f01|f21'
 print(json.dumps(make_variant(*sys.argv[1:]),indent=2))
