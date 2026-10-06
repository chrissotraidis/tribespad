#!/usr/bin/env python3
"""Record original chunkmemset and safe-wrapper outcomes with modeled vector semantics."""
import hashlib, json, pathlib, struct
import pefile, unicorn
from unicorn import x86_const as X
from qol_checksum_vector_reference import VectorReference
R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
image = R / 'ref/product/IFC22.dll'
assert sha(image) == 'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
pe = pefile.PE(str(image)); mapped = pe.get_memory_mapped_image()
u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
u.mem_map(0x10000000, (len(mapped)+4095)&~4095); u.mem_write(0x10000000, mapped)
u.mem_map(0x200000, 0x20000); u.mem_map(0x300000, 0x4000); u.mem_map(0x400000, 0x1000)
model = VectorReference(u, [(0x100c7740, 0x100c77b8), (0x100c77c0, 0x100c7ac0)])
u.hook_add(unicorn.UC_HOOK_CODE, model.hook)
distances = list(range(-33,0)) + list(range(1,34)) + [-65,65]
specifications = [(length,distance,False,0)
                  for length in list(range(1,34))+[63,64,65,127,128,129,255,256,257]
                  for distance in distances]
specifications += [(length,distance,True,left)
                   for length in [0,1,2,3,4,15,16,17,31,32,33,64,65]
                   for left in [0,1,2,3,15,16,31,32,64,128]
                   for distance in [-33,-17,-3,-1,1,2,3,6,10,11,12,15,16,17,31,32,33]]
initial = bytes(((j-0x400)*29+7)&255 for j in range(0x4000))
cases = []
for number, (length, distance, safe, left) in enumerate(specifications):
    source = 0x300400
    destination = 0x300400 + distance
    u.mem_write(0x300000, initial)
    count = min(length,left) if safe else length
    sp = 0x21f003; args = [destination,source,length]+([left] if safe else [])
    stack = struct.pack('<'+str(len(args)+1)+'I',0x400000,*args)
    u.mem_write(sp, stack); u.reg_write(X.UC_X86_REG_ESP, sp); u.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
    preserved = [(r,0x13570000+j*0x10203) for j,r in enumerate(
        [X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI])]
    for r, value in preserved: u.reg_write(r, value)
    model.v = [bytes([j+1])*64 for j in range(8)]; model.k = [0]*8
    u.emu_start(0x100c7a80 if safe else 0x100c77c0, 0x400000, count=100000)
    assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
    assert u.reg_read(X.UC_X86_REG_EAX)==destination+count
    assert all(u.reg_read(r)==value for r,value in preserved)
    assert bytes(u.mem_read(sp,len(stack)))==stack
    upper_cleared = all(v[16:]==bytes(48) for v in model.v)
    assert upper_cleared == (not safe or count > ((-destination)&3))
    after = bytes(u.mem_read(0x300000, len(initial)))
    payload = after[0x400+distance:0x400+distance+count] if count else b''
    expected = bytearray(initial)
    if count: expected[0x400+distance:0x400+distance+count]=payload
    assert after==expected, 'write outside requested output'
    cases.append({'length':length,'distance':distance,'safe':safe,'left':left,'count':count,
                  'upper_cleared':upper_cleared,'destination_after_hex':payload.hex()})
out = R/'generated/qol-chunkmemset-reference'; out.mkdir(exist_ok=True)
paths = [pathlib.Path(__file__).resolve(), R/'scripts/qol_checksum_vector_reference.py']
report = {'pass':True,'image_sha256':sha(image),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},
          'cases':cases,'vector_sites':[dict(pc=hex(pc),**v) for pc,v in sorted(model.sites.items())],
          'scope':'Original x86 chunkmemset/safe scalar control flow and actual CRT calls, with explicit BZHI/mask/vector semantics. Distances in both directions, length/left boundaries, patterned output, guards, callee-saved registers and conditional upper clearing. No substituted calls. Not hardware AVX512 execution or full game evidence.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',len(cases),'original chunkmemset/safe modeled cases')
