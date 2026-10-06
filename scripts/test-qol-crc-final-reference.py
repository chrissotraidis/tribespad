#!/usr/bin/env python3
"""Record original optimized CRC finalization over its guest fold-state bytes."""
import hashlib,json,pathlib,random,struct
import pefile,unicorn
from unicorn import x86_const as X
from qol_checksum_vector_reference import VectorReference
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
image=R/'ref/product/IFC22.dll';assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
m=pefile.PE(str(image)).get_memory_mapped_image();u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
u.mem_map(0x10000000,(len(m)+4095)&~4095);u.mem_write(0x10000000,m)
u.mem_map(0x200000,0x20000);u.mem_map(0x300000,0x1000);u.mem_map(0x400000,0x1000)
model=VectorReference(u,[(0x100c9ba0,0x100c9c63)]);u.hook_add(unicorn.UC_HOOK_CODE,model.hook)
rng=random.Random(0x32cf0d);specs=[bytes(64),bytes([255])*64,bytes([0xaa,0x55])*32]
for bit in range(512):
 b=bytearray(64);b[bit//8]=1<<(bit%8);specs.append(bytes(b))
specs += [bytes(rng.randrange(256) for _ in range(64)) for _ in range(256)]
cases=[]
for fold in specs:
 value=rng.getrandbits(32);arena=bytes([0xa6])*17+fold+struct.pack('<I',value)+bytes([0xa6])*32
 u.mem_write(0x300000,arena);sp=0x21f003;stack=struct.pack('<2I',0x400000,0x300011)
 u.mem_write(sp,stack);u.reg_write(X.UC_X86_REG_ESP,sp);u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
 preserved=[(r,rng.getrandbits(32)) for r in [X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI]]
 for r,v in preserved:u.reg_write(r,v)
 model.v=[bytes([j+1])*64 for j in range(8)];model.k=[0]*8
 u.emu_start(0x100c9ba0,0x400000,count=10000)
 assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
 result=u.reg_read(X.UC_X86_REG_EAX);expected=arena[:81]+struct.pack('<I',result)+arena[85:]
 assert bytes(u.mem_read(0x300000,len(arena)))==expected
 assert bytes(u.mem_read(sp,len(stack)))==stack and all(u.reg_read(r)==v for r,v in preserved)
 assert u.reg_read(X.UC_X86_REG_EFLAGS)==0x202
 assert all(model.v[j][16:]==bytes(48) for j in range(5))
 assert all(model.v[j]==bytes([j+1])*64 for j in range(5,8))
 cases.append(dict(fold_hex=fold.hex(),previous_value=value,result=result))
assert model.tail_calls==0
out=R/'generated/qol-crc-final-reference';out.mkdir(exist_ok=True)
report={'pass':True,'image_sha256':sha(image),'source_sha256':{str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/qol_checksum_vector_reference.py']},'entry':'0x100c9ba0','end_exclusive':'0x100c9c63','body_sha256':hashlib.sha256(m[0xc9ba0:0xc9c63]).hexdigest(),'constants':{'start':'0x10149650','bytes_hex':m[0x149650:0x1496a0].hex(),'sha256':hashlib.sha256(m[0x149650:0x1496a0]).hexdigest()},'cases':cases,'vector_sites':[dict(pc=hex(pc),**v) for pc,v in sorted(model.sites.items())],'scope':'Original scalar control flow plus explicit vector model for CRC fold finalization;771 arbitrary64-byte fold states including512 one-bit basis states. Only value atoffset64 is written; flags/callee GPR/stack preserved; YMM upper0..4 cleared and5..7 preserved. No substituted callees. Not a checksum of actual input data, hardware EVEX, native integration or gameplay.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(cases),'original CRC fold-final modeled states')
