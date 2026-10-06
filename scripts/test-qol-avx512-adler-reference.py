#!/usr/bin/env python3
"""Execute pinned AVX512 Adler scalar flow with a finite offline vector model."""
import hashlib, json, pathlib, struct, zlib
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
u.mem_map(0x200000, 0x20000); u.mem_map(0x300000, 0x40000); u.mem_map(0x400000, 0x1000)
model = VectorReference(u); u.hook_add(unicorn.UC_HOOK_CODE, model.hook)
cases = []
specifications = []
for pattern in [0, 1]:
    for length in [0, 1, 15, 16, 31, 32, 63, 64, 65, 127, 128, 5551, 5552, 5553, 16384, 65536]:
        for seed in [0, 1, 0xffff, 0xffffffff, 0x89abcdef]:
            for target in ['checksum', 'copy', 'core-copy2']:
                specifications.append((length, seed, target, pattern, False, False))
for seed in [0, 1, 0xffff, 0xffffffff, 0x89abcdef]:
    for length in [0, 1, 0xffffffff]:
        for target in ['checksum', 'copy', 'core-copy2']:
            specifications.append((length, seed, target, 0, True, False))
    for target in ['copy', 'core-copy2']:
        specifications.append((0, seed, target, 0, False, True))
for number, (length, seed, target, pattern, source_null, destination_null) in enumerate(specifications):
    source = 0 if source_null else 0x300011
    destination = 0 if destination_null else 0x320017
    data = bytes([255]) * length if pattern and not source_null else bytes((j*17+length*3)&255 for j in range(length)) if not source_null else b''
    source_bytes = b'\xa6'*17 + data + b'\xa6'*32
    destination_bytes = b'\xb7'*(23+len(data)+32)
    u.mem_write(0x300000, source_bytes); u.mem_write(0x320000, destination_bytes)
    entry = {'checksum':0x100c74e0, 'copy':0x100c7500, 'core-copy2':0x100c7520}[target]
    args = [seed, source, length] if target == 'checksum' else [seed, destination, source, length]
    if target == 'core-copy2': args.append(2)
    sp = 0x21f003; stack = struct.pack('<' + str(len(args)+1) + 'I', 0x400000, *args)
    u.mem_write(sp, stack); u.reg_write(X.UC_X86_REG_ESP, sp); u.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
    preserved = [(r, 0x13570000+j*0x10203) for j,r in enumerate(
        [X.UC_X86_REG_EBX,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI])]
    for r, value in preserved: u.reg_write(r, value)
    model.v = [bytes([j+1])*64 for j in range(8)]; model.k = [0] * 8
    u.emu_start(entry, 0x400000, count=5000000)
    assert u.reg_read(X.UC_X86_REG_EIP) == 0x400000 and u.reg_read(X.UC_X86_REG_ESP) == sp+4
    result = u.reg_read(X.UC_X86_REG_EAX)
    expected = 1 if source_null else seed if not length else zlib.adler32(data, seed)
    assert result == expected, (number, length, hex(seed), target, hex(result), hex(expected))
    assert all(u.reg_read(r) == value for r, value in preserved)
    stack_after = bytes(u.mem_read(sp, len(stack)))
    if target != 'core-copy2': assert stack_after == stack
    assert bytes(u.mem_read(0x300000, len(source_bytes))) == source_bytes
    wanted = destination_bytes if target == 'checksum' or source_null or not length else destination_bytes[:23]+data+destination_bytes[23+length:]
    assert bytes(u.mem_read(0x320000, len(wanted))) == wanted
    cases.append({'length':length,'seed':seed,'target':target,'pattern':pattern,
                  'source_null':source_null,'destination_null':destination_null,'result':result,
                  'argument_words_after':list(struct.unpack('<' + str(len(args)) + 'I', stack_after[4:]))})
    if number % 100 == 0: print('Verified AVX512 Adler modeled case', number, flush=True)
out = R / 'generated/qol-avx512-adler-reference'; out.mkdir(exist_ok=True)
paths = [pathlib.Path(__file__).resolve(), R/'scripts/qol_checksum_vector_reference.py']
report = {'pass':True,'image_sha256':sha(image),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},
          'cases':cases,'vector_sites':[dict(pc=hex(pc),**v) for pc,v in sorted(model.sites.items())],
          'scalar_tail_substitutions':model.tail_calls,
          'scope':'Original x86 wrapper/core scalar control flow with finite Python vector semantics at exact original sites. AVX2 positive tails below64 explicitly redirected to original scalar checksum leaf. Matches independent Python zlib, zero/NULL contracts, guards and callee-saved registers. Not unmodified hardware AVX512 execution, original AVX2 tail equivalence proof, native whole-DLL execution or gameplay.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',len(cases),'AVX512 Adler modeled cases with explicit scalar tail substitution')
