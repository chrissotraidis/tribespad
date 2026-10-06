#!/usr/bin/env python3
"""Execute original CRC32 input/fold/final flow with explicit vector semantics."""
import hashlib,json,pathlib,struct,zlib
import pefile,unicorn
from unicorn import x86_const as X
from qol_checksum_vector_reference import VectorReference
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/product/IFC22.dll';assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
m=pefile.PE(str(image)).get_memory_mapped_image();u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
u.mem_map(0x10000000,(len(m)+4095)&~4095);u.mem_write(0x10000000,m)
u.mem_map(0x200000,0x20000);u.mem_map(0x300000,0x20000);u.mem_map(0x400000,0x1000)
model=VectorReference(u,[(0x100c9150,0x100ca2b9)]);u.hook_add(unicorn.UC_HOOK_CODE,model.hook)
lengths=list(range(0,81))+[127,128,129,255,256,257,511,512,513,1023,1024,1025,16384,65536]
specs=[(length,seed,pattern,False) for length in lengths for seed in [0,1,0xffffffff,0x89abcdef] for pattern in [0,1]]
specs += [(0,seed,0,True) for seed in [0,1,0xffffffff,0x89abcdef]]
cases=[]
for number,(length,seed,pattern,null) in enumerate(specs):
 data=bytes([255])*length if pattern else bytes((j*17+length*3)&255 for j in range(length))
 arena=b'\xa6'*17+data+b'\xa6'*32;u.mem_write(0x300000,arena)
 sp=0x21f003;source=0 if null else 0x300011;stack=struct.pack('<4I',0x400000,seed,source,length)
 u.mem_write(sp,stack);u.reg_write(X.UC_X86_REG_ESP,sp);u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
 preserved=[(r,0x13570000+j*0x10203) for j,r in enumerate([X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI])]
 for r,v in preserved:u.reg_write(r,v)
 model.v=[bytes([j+1])*64 for j in range(8)];model.k=[0]*8
 u.emu_start(0x100c9ca0,0x400000,count=5000000)
 assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
 result=u.reg_read(X.UC_X86_REG_EAX);expected=zlib.crc32(data,seed)
 assert result==expected,(number,length,hex(seed),hex(result),hex(expected))
 assert bytes(u.mem_read(0x300000,len(arena)))==arena
 assert bytes(u.mem_read(sp,len(stack)))==stack and all(u.reg_read(r)==v for r,v in preserved)
 if length>=16:assert all(v[16:]==bytes(48) for v in model.v)
 else:assert model.v==[bytes([j+1])*64 for j in range(8)]
 cases.append(dict(length=length,seed=seed,pattern=pattern,source_null=null,result=result,upper_cleared=length>=16))
 if number%100==0:print('Verified original CRC32 case',number,flush=True)
faults=[]
for length in [1,15,16,17,32,65536]:
 for seed in [0,0xffffffff]:
  sp=0x21f003;u.mem_write(sp,struct.pack('<4I',0x400000,seed,0,length));u.reg_write(X.UC_X86_REG_ESP,sp);u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
  model.v=[bytes([j+1])*64 for j in range(8)];model.k=[0]*8
  try:u.emu_start(0x100c9ca0,0x400000,count=5000000)
  except unicorn.UcError as e:
   assert e.errno==unicorn.UC_ERR_READ_UNMAPPED
   faults.append(dict(length=length,seed=seed,error=e.errno,pc=hex(u.reg_read(X.UC_X86_REG_EIP))))
  else:raise AssertionError('positive NULL CRC input accepted')
assert model.tail_calls==0
out=R/'generated/qol-crc32-reference';out.mkdir(exist_ok=True)
report={'pass':True,'image_sha256':sha(image),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/qol_checksum_vector_reference.py']},'entry':'0x100c9ca0','end_exclusive':'0x100c9e00','body_sha256':hashlib.sha256(m[0xc9ca0:0xc9e00]).hexdigest(),'cases':cases,'null_positive_faults':faults,'vector_sites':[dict(pc=hex(pc),**v) for pc,v in sorted(model.sites.items())],'scope':'Original CRC32 scalar/fold/final flow including original EVEX bulk helpers and CRT calls; explicit offline vector model without substituted callees. Input checksum agrees with Python zlib,guards/stack/callee GPR/conditional upper clearing checked. NULL positive input remains unsupported. Not hardwareEVEX/native wholemodule/gameplay.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(cases),'original CRC32 modeled actual-input cases')
