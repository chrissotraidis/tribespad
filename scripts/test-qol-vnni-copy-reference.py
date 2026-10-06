#!/usr/bin/env python3
"""Execute original VNNI checksum-copy flow with a finite offline vector model."""
import hashlib, json, pathlib, struct, zlib
import pefile, unicorn
from unicorn import x86_const as X
from qol_checksum_vector_reference import VectorReference
R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
image = R/'ref/product/IFC22.dll'
assert sha(image) == 'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
pe = pefile.PE(str(image)); mapped = pe.get_memory_mapped_image()
u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
u.mem_map(0x10000000, (len(mapped)+4095)&~4095); u.mem_write(0x10000000, mapped)
u.mem_map(0x200000,0x20000); u.mem_map(0x300000,0x40000); u.mem_map(0x400000,0x1000)
model = VectorReference(u,[(0x100c8380,0x100c8579)])
u.hook_add(unicorn.UC_HOOK_CODE,model.hook)
lengths = list(range(0,131)) + [255,256,257,5551,5552,5553,11103,11104,11105,16384,65535,65536]
specs = [(length,seed,pattern,False,False) for pattern in [0,1] for length in lengths
         for seed in [0,1,0xffff,0xffffffff,0x89abcdef]]
specs += [(length,seed,0,True,destination_null) for length in [0,1,0xffffffff]
          for seed in [0,1,0xffff,0xffffffff,0x89abcdef] for destination_null in [False,True]]
specs += [(0,seed,0,False,True) for seed in [0,1,0xffff,0xffffffff,0x89abcdef]]
cases=[]
for number,(length,seed,pattern,null,destination_null) in enumerate(specs):
    data = b'' if null else bytes([255])*length if pattern else bytes((j*17+length*3)&255 for j in range(length))
    arena = b'\xa6'*17 + data + b'\xa6'*32
    u.mem_write(0x300000,arena)
    source=0 if null else 0x300011
    destination=0 if destination_null else 0x320017
    destination_before=b'\xb7'*(23+len(data)+32)
    u.mem_write(0x320000,destination_before)
    sp=0x21f003; stack=struct.pack('<5I',0x400000,seed,destination,source,length)
    u.mem_write(sp,stack); u.reg_write(X.UC_X86_REG_ESP,sp); u.reg_write(X.UC_X86_REG_EFLAGS,0x202)
    preserved=[(r,0x13570000+j*0x10203) for j,r in enumerate(
        [X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI])]
    for r,value in preserved:u.reg_write(r,value)
    model.v=[bytes([j+1])*64 for j in range(8)]; model.k=[0]*8
    u.emu_start(0x100c8380,0x400000,count=5000000)
    assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
    expected=1 if null else seed if not length else zlib.adler32(data,seed)
    result=u.reg_read(X.UC_X86_REG_EAX)
    assert result==expected,(number,length,hex(seed),hex(result),hex(expected))
    assert all(u.reg_read(r)==v for r,v in preserved)
    assert bytes(u.mem_read(sp,len(stack)))==stack
    assert bytes(u.mem_read(0x300000,len(arena)))==arena
    wanted=destination_before if null or not length else destination_before[:23]+data+destination_before[23+length:]
    assert bytes(u.mem_read(0x320000,len(wanted)))==wanted
    assert all(v[16:]==bytes(48) for v in model.v)
    cases.append(dict(length=length,seed=seed,pattern=pattern,source_null=null,destination_null=destination_null,result=result))
    if number%250==0:print('Verified VNNI checksum case',number,flush=True)
out=R/'generated/qol-vnni-copy-reference';out.mkdir(exist_ok=True)
paths=[pathlib.Path(__file__).resolve(),R/'scripts/qol_checksum_vector_reference.py']
report={'pass':True,'image_sha256':sha(image),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},
        'entry':'0x100c8380','end_exclusive':'0x100c8579','body_sha256':hashlib.sha256(mapped[0xc8380:0xc8579]).hexdigest(),
        'cases':cases,'vector_sites':[dict(pc=hex(pc),**v) for pc,v in sorted(model.sites.items())],
        'scalar_tail_substitutions':model.tail_calls,
        'scope':'Original VNNI checksum-copy scalar control flow and original SSSE3 short tails in Unicorn, with finite vector/SHRX/mask semantics at exact sites. No substituted callees. Python zlib verifies checksum; complete source/destination guards, NULL/zero contracts, preserved GPRs/stack and modeled upper clearing checked. Nonoverlapping copy contract. Not hardware AVX512, native whole-DLL integration or gameplay.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
assert model.tail_calls == 0
print('PASS:',len(cases),'VNNI Adler modeled cases;',model.tail_calls,'explicit scalar-tail substitutions')
