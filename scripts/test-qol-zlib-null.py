#!/usr/bin/env python3
"""Record original scalar checksum NULL/zero-length behavior, without DLL init."""
import hashlib, json, pathlib, struct
import pefile, unicorn
from unicorn import x86_const as X
R = pathlib.Path(__file__).resolve().parents[1]
image = R / 'ref/product/IFC22.dll'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(image) == 'eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
pe = pefile.PE(str(image)); mapped = pe.get_memory_mapped_image()
cases = []
for seed in [0, 1, 0xffff, 0xffffffff, 0x89abcdef]:
    specifications = [(False, True, False, n) for n in [0, 1, 2, 16, 5552, 0xffffffff]]
    specifications += [(True, sn, dn, 0) for sn, dn in [(True, False), (False, True), (True, True)]]
    specifications += [(True, sn, dn, 1) for sn, dn in [(True, False), (False, True), (True, True)]]
    for copy, source_null, destination_null, length in specifications:
        u = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        u.mem_map(0x10000000, (len(mapped) + 4095) & ~4095); u.mem_write(0x10000000, mapped)
        u.mem_map(0x200000, 0x10000); u.mem_map(0x300000, 0x10000); u.mem_map(0x400000, 0x1000)
        # Copy uses the scalar checksum slot established by the separate actual
        # feature-dispatch test. This is a leaf contract test, not DLL init.
        u.mem_write(0x101d547c, struct.pack('<I', 0x100c1c20))
        source = 0 if source_null else 0x300011
        destination = 0 if destination_null else 0x300117
        u.mem_write(0x300000, b'\xa6' * 0x1000)
        args = [seed, destination, source, length] if copy else [seed, source, length]
        sp = 0x20f003
        stack = struct.pack('<' + str(len(args) + 1) + 'I', 0x400000, *args)
        u.mem_write(sp, stack); u.reg_write(X.UC_X86_REG_ESP, sp)
        preserved = [(r, 0x13570000 + j * 0x10203) for j, r in enumerate(
            [X.UC_X86_REG_EBX, X.UC_X86_REG_EBP, X.UC_X86_REG_ESI, X.UC_X86_REG_EDI])]
        for r, value in preserved: u.reg_write(r, value)
        error = None
        try: u.emu_start(0x100c1bf0 if copy else 0x100c1c20, 0x400000, count=100000)
        except unicorn.UcError as e: error = e.errno
        fault = length == 1
        if fault:
            assert error == unicorn.UC_ERR_READ_UNMAPPED or (copy and error == unicorn.UC_ERR_WRITE_UNMAPPED)
            result = None
        else:
            assert error is None and u.reg_read(X.UC_X86_REG_EIP) == 0x400000
            assert u.reg_read(X.UC_X86_REG_ESP) == sp + 4
            assert all(u.reg_read(r) == v for r, v in preserved)
            assert bytes(u.mem_read(sp, len(stack))) == stack
            result = u.reg_read(X.UC_X86_REG_EAX)
            assert result == (1 if source_null else ((seed & 0xffff) % 65521) | (((seed >> 16) % 65521) << 16))
        assert bytes(u.mem_read(0x300000, 0x1000)) == b'\xa6' * 0x1000
        cases.append({'seed': seed, 'copy': copy, 'source_null': source_null,
                      'destination_null': destination_null, 'length': length,
                      'fault': fault, 'unicorn_error': error, 'result': result})
out = R / 'generated/qol-zlib-null'; out.mkdir(exist_ok=True)
report = {'pass': True, 'image_sha256': sha(image), 'script_sha256': sha(pathlib.Path(__file__)),
          'cases': cases, 'scope': 'Original x86 scalar leaves only, known scalar checksum pointer installed for copy. Nonfaulting NULL checksums and zero-length copies; one-byte NULL accesses fault. No native Windows exception delivery or DLL initialization claim.'}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: 60 original scalar NULL/zero-length cases, including20 unmapped-access faults')
