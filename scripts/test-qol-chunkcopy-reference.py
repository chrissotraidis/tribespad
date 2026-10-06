#!/usr/bin/env python3
"""Execute original fixed32 chunk-copy scalar flow with explicit vector semantics."""
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
model = VectorReference(u, [(0x100c7740, 0x100c77b8)])
u.hook_add(unicorn.UC_HOOK_CODE, model.hook)
distances = [-65,-33,-32,-31,-16,-1,0,1,15,16,31,32,33,65]
specifications = [(length, distance, False, False)
                  for length in list(range(81))+[127,128,129,255,256,257,1023,1024]
                  for distance in distances]
specifications += [(0, 0, sn, dn) for sn, dn in [(True,False),(False,True),(True,True)]]
initial = bytes(((j-0x400)*29+7)&255 for j in range(0x4000))
cases = []
for number, (length, distance, source_null, destination_null) in enumerate(specifications):
    source = 0 if source_null else 0x300400
    destination = 0 if destination_null else 0x300400 + distance
    u.mem_write(0x300000, initial)
    sp = 0x21f003; stack = struct.pack('<4I', 0x400000, destination, source, length)
    u.mem_write(sp, stack); u.reg_write(X.UC_X86_REG_ESP, sp); u.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
    preserved = [(r,0x13570000+j*0x10203) for j,r in enumerate(
        [X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI])]
    for r, value in preserved: u.reg_write(r, value)
    model.v = [bytes([j+1])*64 for j in range(8)]; model.k = [0]*8
    u.emu_start(0x100c7740, 0x400000, count=100000)
    assert u.reg_read(X.UC_X86_REG_EIP)==0x400000 and u.reg_read(X.UC_X86_REG_ESP)==sp+4
    assert u.reg_read(X.UC_X86_REG_EAX)==destination+length
    assert all(u.reg_read(r)==value for r,value in preserved)
    assert bytes(u.mem_read(sp,len(stack)))==stack
    assert all(v[16:]==bytes(48) for v in model.v)
    after = bytes(u.mem_read(0x300000, len(initial)))
    payload = after[0x400+distance:0x400+distance+length] if length else b''
    expected = bytearray(initial)
    if length: expected[0x400+distance:0x400+distance+length]=payload
    assert after==expected, 'write outside requested output'
    cases.append({'length':length,'distance':distance,'source_null':source_null,
                  'destination_null':destination_null,'destination_after_hex':payload.hex()})
out = R/'generated/qol-chunkcopy-reference'; out.mkdir(exist_ok=True)
paths = [pathlib.Path(__file__).resolve(), R/'scripts/qol_checksum_vector_reference.py']
report = {'pass':True,'image_sha256':sha(image),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},
          'cases':cases,'vector_sites':[dict(pc=hex(pc),**v) for pc,v in sorted(model.sites.items())],
          'scope':'Original x86 fixed32 helper scalar control flow with explicit BZHI/KMOVD/vector load-store model. Overlap order, exact requested output, zero-mask NULL behavior, callee-saved registers, stack and VZEROUPPER. No substituted calls. Not hardware AVX512 execution or full game evidence.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',len(cases),'original fixed32 chunk-copy modeled cases')
