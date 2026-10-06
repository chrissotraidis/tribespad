#!/usr/bin/env python3
"""Prepare the reached audio/startup CALL group as one finite AOT revision."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/startup-calls';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
frozen=R/'generated/integration-review/sdl-tls-checkpoint-20261004';cap_path=frozen/'generated/integration-review/sdl-tls-execution/next-patch.json';manifest=json.loads((frozen/'checkpoint.json').read_text());assert sha(cap_path)==manifest['files'][str(cap_path.relative_to(frozen))];cap=json.loads(cap_path.read_text());assert cap['writer']=='0x10055719' and cap['memory']['0x10470a54']=='00000000'
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';s=engine.read_text();variants=[];rows=[]
for entry,call,target in [(0x410c79,0x410c79,0x10055570),(0x40a130,0x40a131,0x1005a2a0),(0x40a640,0x40a641,0x1005a2a0)]:
 name='loc_%X'%entry;body=definition(s,name);original=bytes.fromhex(cap['memory'][hex(call)]);after=b'\xe8'+((target-call-5)&0xffffffff).to_bytes(4,'little');before=original[:5]
 if entry==call:
  assert original==bytes.fromhex('ff1564c97d00');after+=b'\x90';before=original
  changed=body.replace('store i32 4263039,','store i32 4263038,').replace('@tbimp__AIL_startup_400(', '@loc_10055570(')
 else:
  assert original[0]==0xe8;changed=body.replace('@loc_566FC0(', '@loc_1005A2A0(');prefix=b'\x53' if entry==0x40a130 else b'\x56';before=prefix+before;after=prefix+after
 assert changed!=body;variants.append(changed);rows.append(dict(name=name,entry=entry,end=entry+len(after),target=target,before_bytes=before.hex(),after_bytes=after.hex(),old_definition_sha256=hashlib.sha256(body.encode()).hexdigest()))
# The shortened CALL returns to a real original address now containing NOP.
variants.append('''define hidden fastcc void @loc_410C7E(%_cpu* %cpu) nounwind {
 %ready = call i32 @qol_startup_calls_route_ready()
 %ok = icmp ne i32 %ready, 0
 br i1 %ok, label %active, label %invalid
active:
 musttail call fastcc void @loc_410C7F(%_cpu* %cpu)
 ret void
invalid:
 call void @qol_startup_calls_unavailable()
 unreachable
}
''')
rows.append(dict(name='loc_410C7E',entry=0x410c7e,end=0x410c7f,target=0x410c7f,after_bytes='90',added=True))
pre=s[:s.index('define ')];declarations='\n'.join('declare hidden fastcc void @loc_%X(%%_cpu*)'%t for t in sorted({r['target'] for r in rows}));(out/'engine.ll').write_text(pre+declarations+'\ndeclare i32 @qol_startup_calls_route_ready()\ndeclare void @qol_startup_calls_unavailable() noreturn\n'+''.join(variants))
writes=[]
for pc,address,size,value in [(0x10055719,0x410c79,1,0xe8),(0x10055720,0x410c7a,4,0x10055570-0x410c7e),(0x10055722,0x410c7e,1,0x90),(0x100557a3,0x40a132,4,0x1005a2a0-0x40a136),(0x100557f1,0x40a642,4,0x1005a2a0-0x40a646)]:
 root=0x410c79 if address<0x410c7f and address>=0x410c79 else address-1;raw=bytes.fromhex(cap['memory'][hex(root)]);before=int.from_bytes(raw[address-root:address-root+size],'little');writes.append(dict(pc=pc,address=address,size=size,before=before,value=value))
report=dict(passed=True,published=False,records=rows,writes=writes,inputs_sha256={str(p.relative_to(R)):sha(p) for p in [cap_path,engine,pathlib.Path(__file__).resolve()]},outputs_sha256={'engine.ll':sha(out/'engine.ll')},scope='Three precompiled call variants and the shortened-call NOP return. Five exact original stores; subsequent native protection results remain required by unchanged control flow.')
(out/'preparation.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared three CALL variants, NOP return and five stores')
