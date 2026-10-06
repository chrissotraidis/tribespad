#!/usr/bin/env python3
"""Prepare the seven original renderer stores before the next subsystem call."""
import hashlib,json,pathlib,sys,struct,re
import pefile,capstone
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-startup';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';image=R/'ref/control/Tribes2.exe';p=pefile.PE(str(image));s=engine.read_text();capture=R/'generated/integration-review/resources/execution/next-write.json';observed=json.loads(capture.read_text());assert observed['replacement']=='e95b010000' and observed['target_before'].startswith('80bd44faff')
spans=[('loc_5A1130',0x5a1130,0x5a1147,0x5a1290),('loc_5C46DB',0x5c46db,0x5c46ea,0x10041440),('loc_4E81F7',0x4e81f7,0x4e820a,0x10041390)]
writes=[];data={a:bytearray(p.get_data(a-0x400000,b-a)) for _,a,b,_ in spans}
for pc,address,size,value in [(0x10047342,0x5a1130,4,0x00015be9),(0x10047349,0x5a1134,1,0),(0x100473d4,0x5c46e6,4,0x10041440-0x5c46ea),(0x1004741d,0x4e81fd,4,0x90909090),(0x1004741f,0x4e8201,2,0x9090),(0x1004743b,0x4e81fd,2,0xd989),(0x10047492,0x4e8206,4,0x10041390-0x4e820a)]:
 start=next(a for _,a,b,_ in spans if a<=address and address+size<=b);buf=data[start];offset=address-start;before=int.from_bytes(buf[offset:offset+size],'little');buf[offset:offset+size]=value.to_bytes(size,'little');writes.append(dict(pc=pc,address=address,size=size,before=before,value=value))
assert p.get_data(0x1c46e5,1)==p.get_data(0xe8205,1)==b'\xe8'
cpu=re.search(r'^%_cpu = type .*$',s,re.M)[0];variants=[];records=[]
for name,start,end,target in spans:
 body=definition(s,name)
 if start==0x5a1130:changed='define hidden fastcc void @loc_5A1130(%_cpu* %cpu) nounwind {\n musttail call fastcc void @loc_5A1290(%_cpu* %cpu) nounwind\n ret void\n}\n'
 elif start==0x5c46db:
  assert body.count('@loc_4AF470(')==1;changed=body.replace('@loc_4AF470(','@loc_10041440(')
 else:
  assert body.count('%15 = load i32, i32* %14, align 1')==1 and body.count('@loc_4B6CC0(')==1
  changed=body.replace('%15 = load i32, i32* %14, align 1','%15 = add i32 %8, 0 ; original patched MOV ECX, EBX').replace('@loc_4B6CC0(','@loc_10041390(')
 variants.append(changed);records.append(dict(name=name,entry=start,end=end,target=target,before_bytes=p.get_data(start-0x400000,end-start).hex(),after_bytes=data[start].hex(),before_sha256=hashlib.sha256(body.encode()).hexdigest()))
(out/'engine.ll').write_text(cpu+'\n'+''.join('declare hidden fastcc void @loc_%X(%%_cpu*)\n'%v[3] for v in spans)+''.join(variants))
meta=dict(passed=True,records=records,writes=writes,inputs_sha256={str(q.relative_to(R)):sha(q) for q in [engine,image,capture,pathlib.Path(__file__).resolve()]},outputs_sha256={'engine.ll':sha(out/'engine.ll')},scope='Original seven-store prefix, including six NOP bytes then overlapping MOV ECX,EBX. Existing direct CALL forms only; unobserved FF alternatives remain guarded. Original protection branches and first flush remain.')
(out/'preparation.json').write_text(json.dumps(meta,indent=2)+'\n');print('Prepared 3 renderer blocks / 7 ordered stores')
