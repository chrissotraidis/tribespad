#!/usr/bin/env python3
"""Compare linked captured trampolines and real continuation blocks with x86."""
import argparse
import ctypes
import hashlib
import json
import pathlib
import random
import re
import struct
import subprocess

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

R = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--patched-engine', action='store_true')
a = p.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
out = R / 'generated/integration-review/native-continuations'
meta = json.loads((out / 'result.json').read_text())
assert meta['state'] == 'compiled_unpublished'
bank = R / 'generated/integration-review/native-trampolines'
assert sha(bank / 'result.json') == meta['bank_metadata_sha256']
bank_meta = json.loads((bank / 'result.json').read_text())
assert sha(bank / 'module.o') == bank_meta['object_sha256']
assert sha(out / 'bridges.ll') == meta['bridges_sha256']
assert sha(out / 'dispatch-entries.json') == meta['dispatch_entries_sha256']
assert sha(bank / 'dispatch-entries.json') == bank_meta['dispatch_entries_sha256']
plan_path = R / bank_meta['plan']
assert sha(plan_path) == bank_meta['plan_sha256']
plan = json.loads(plan_path.read_text())
entries = json.loads((bank / 'dispatch-entries.json').read_text()) + json.loads((out / 'dispatch-entries.json').read_text())
targets = set()
for owner, row in meta['modules'].items():
    assert sha(out / owner / 'module.o') == row['object_sha256']
    assert sha(out / owner / 'module.ll') == row['module_sha256']
    assert all(sha(R / n) == h for n, h in row['helper_sources_sha256'].items())
    targets.update(int(n[4:], 16) for n in row['external_targets'])
    targets.update(row['existing_targets'])
engine_object = out / 'engine/module.o'
test_out = out
patched_identity = None
if a.patched_engine:
    patched = R / 'generated/integration-review/patched-engine'
    patched_identity = json.loads((patched / 'result.json').read_text())
    assert patched_identity['native_plan_sha256'] == sha(plan_path)
    assert patched_identity['state'] == 'strictly_lifted_unpublished'
    assert sha(patched / 'seg01_code.llinc') == patched_identity['code_sha256']
    compiled = json.loads((patched / 'va/compile.json').read_text())
    assert compiled['exit'] == 0 and sha(patched / 'va/engine.o') == compiled['object_sha256']
    assert sha(patched / 'va/engine.ll') == compiled['input_sha256']
    reference = R / plan['reference']
    assert sha(reference) == plan['reference_sha256']
    patch_plan = json.loads(reference.read_text())
    llasm = (patched / 'seg01_code.llinc').read_text()
    roots = set(meta['modules']['engine']['new_entries'])
    roots.update('loc_%X' % row['entry'] for row in patch_plan['entry_redirects'])
    for row in patch_plan['code_calls']:
        pos = llasm.index('PUSH loc_%X\ntcall loc_%X\n' % (row['call_site'] + 5, row['new_target']))
        start = llasm.rfind('\nproc ', 0, pos)
        roots.add(llasm[start:].splitlines()[1].split()[1])
    # Extract complete, unchanged LLVM function definitions from the exact
    # whole-engine compiler input. Downstream functions alone become observers.
    preamble, definitions = [], {}
    active, lines, started = None, [], False
    with (patched / 'va/engine.ll').open() as f:
        for line in f:
            if line.startswith('define '):
                started = True
                match = re.search(r'@(loc_[0-9A-F]+)\(', line)
                active = match[1] if match and match[1] in roots else None
                lines = []
            if not started: preamble.append(line)
            if active:
                lines.append(line)
                if line.strip() == '}':
                    definitions[active] = ''.join(lines)
                    active = None
    assert set(definitions) == roots
    bodies = '\n'.join(definitions[n] for n in sorted(roots))
    refs = set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(', bodies)) - roots
    assert all(re.fullmatch('loc_[0-9A-F]+', n) or n == 'tb_x87_fclex' for n in refs), refs
    declared = set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(', ''.join(preamble)))
    declarations = ['declare fastcc void @%s(%%_cpu*)\n' % n for n in sorted(refs - declared)]
    test_out = out / 'patched'
    test_out.mkdir(exist_ok=True)
    (test_out / 'engine-fragments.ll').write_text(''.join(preamble + declarations) + bodies)
    engine_object = test_out / 'engine-fragments.o'
    subprocess.run(['clang', '-target', 'arm64-apple-macos12', '-O0', '-Wno-override-module',
                    '-c', str(test_out / 'engine-fragments.ll'), '-o', str(engine_object)], check=True)
    targets = {int(n[4:], 16) for n in refs if n.startswith('loc_')}
    targets.update(meta['modules']['engine']['existing_targets'])
    targets.update(int(n[4:], 16) for n in meta['modules']['qol']['external_targets'])
    entries = [e for e in entries if e.get('owner') != 'engine'] + [dict(address=int(n[4:], 16), symbol=n, owner='engine') for n in sorted(roots)]
    patched_identity = dict(preparation_sha256=sha(patched / 'result.json'),
        whole_engine_object_sha256=compiled['object_sha256'], whole_engine_llvm_sha256=compiled['input_sha256'],
        fragment_llvm_sha256=sha(test_out / 'engine-fragments.ll'),
        fragment_object_sha256=sha(engine_object), extracted_procedures=sorted(roots))
bridge = ['@observed_target = global i32 0']
for target in sorted(targets):
    bridge.append('define hidden fastcc void @loc_%X(ptr %%cpu) { store i32 %d, ptr @observed_target\n ret void\n}' % (target, target))
for i, row in enumerate(entries):
    bridge += ['declare fastcc void @%s(ptr)' % row['symbol'],
               'define void @enter_%d(ptr %%cpu) { call fastcc void @%s(ptr %%cpu)\n ret void\n}' % (i, row['symbol'])]
(test_out / 'test-bridge.ll').write_text('\n'.join(bridge) + '\n')
sources = [R / n for n in ['port/llasm-support/tribes_x87.c', 'port/core/x87_status.c']]
subprocess.run(['clang', '-target', 'arm64-apple-macos12', '-O2', '-Wno-override-module',
                '-DPTROFS_64BIT', '-dynamiclib', str(bank / 'module.o'),
                str(engine_object), str(out / 'qol/module.o'),
                str(out / 'bridges.ll'), str(test_out / 'test-bridge.ll'),
                *map(str, sources), '-o', str(test_out / 'test.dylib')], check=True)
lib = ctypes.CDLL(str(test_out / 'test.dylib'))
observed = ctypes.c_uint32.in_dll(lib, 'observed_target')
u = Uc(UC_ARCH_X86, UC_MODE_32)
for owner, filename in [('engine', 'Tribes2.exe'), ('qol', 'IFC22.dll')]:
    path = out / owner / filename
    assert sha(path) in meta['modules'][owner]['copied_inputs_sha256'].values()
    pe = pefile.PE(str(path))
    u.mem_map(pe.OPTIONAL_HEADER.ImageBase, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    u.mem_write(pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image())
    if owner == 'engine' and a.patched_engine:
        for row in patch_plan['engine_regions']: u.mem_write(row['address'], bytes.fromhex(row['after_hex']))
bank_address = plan['native_allocation']['address']
u.mem_map(bank_address, 65536)
for row in plan['trampolines']: u.mem_write(row['address'], bytes.fromhex(row['code_hex']))
u.mem_map(0x200000, 65536)
memory = ctypes.create_string_buffer(65536)
offset = ctypes.addressof(memory) - 0x200000
expected_target = [0]


def transfer(uc, address, size, data):
    if address in targets:
        expected_target[0] = address
        uc.emu_stop()


u.hook_add(UC_HOOK_CODE, transfer)
regs = [UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
        UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EFLAGS]
rng = random.Random(25034)
cases = 0
visited = set()
by_tramp = {r['address']: r['continuation'] for r in plan['trampolines']}
for i, row in enumerate(entries):
    fn = getattr(lib, 'enter_%d' % i)
    fn.argtypes = [ctypes.c_void_p]
    for sample in range(256):
        values = [rng.getrandbits(32) for _ in range(8)] + [0x202 | rng.choice([0, 0x8d5, 0x41, 0x884, 0x400])]
        values[4] = 0x208000 + (sample % 16) * 4
        values[5] = 0x208800
        # Vary stack arguments across both sides of every conditional prefix.
        fill = [0, 1, 3, 15, 16, 17, 0x80000000, 0xffffffff][sample % 8]
        pattern = bytearray(struct.pack('<I', fill) * (65536 // 4))
        is_fclex = by_tramp.get(row['address'], row['address']) == 0x6ff315
        if is_fclex:
            # Original function's first argument is a structure pointer.
            struct.pack_into('<I', pattern, values[4] - 0x200000 + 4, 0x200100)
            struct.pack_into('<I', pattern, values[5] - 0x200000 + 8, 0x200100)
            struct.pack_into('<I', pattern, 0x104, fill)
        u.mem_write(0x200000, bytes(pattern))
        ctypes.memmove(memory, bytes(pattern), 65536)
        cpu = ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)), 440)
        struct.pack_into('<9I', cpu, 0, *values)
        top = sample % 8
        sw = rng.getrandbits(16) & ~0xb880
        struct.pack_into('<III', cpu, 36, top, sw, 0x37f)
        struct.pack_into('<Q', cpu, 112, offset)
        before = cpu.raw
        for reg, value in zip(regs, values): u.reg_write(reg, value)
        u.reg_write(UC_X86_REG_FPCW, 0x37f)
        u.reg_write(UC_X86_REG_FPSW, sw | (top << 11))
        expected_target[0] = 0
        u.emu_start(row['address'], 0, count=128)
        assert expected_target[0] in targets, (row, sample, 'no boundary')
        expected = [u.reg_read(reg) for reg in regs]
        observed.value = 0
        fn(cpu)
        assert observed.value == expected_target[0], (row, sample, 'target')
        visited.add(observed.value)
        # AF after TEST/AND/XOR is undefined; exclude it at those boundaries.
        logical = by_tramp.get(row['address'], row['address']) in {0x401ce5, 0x401d95, 0x401e45, 0x6fc356, 0x6fc3a6, 0x6ff315}
        actual = list(struct.unpack_from('<9I', cpu))
        if logical: actual[8] &= ~0x10; expected[8] &= ~0x10
        assert actual == expected, (hex(row['address']), sample, actual, expected)
        after = bytearray(before)
        if is_fclex: struct.pack_into('<I', after, 40, u.reg_read(UC_X86_REG_FPSW) & ~0x3800)
        assert cpu.raw[36:] == after[36:], (row, sample, 'non-GPR CPU state')
        assert memory.raw == bytes(u.mem_read(0x200000, 65536)), (row, sample, 'stack/data')
        cases += 1
assert visited == targets, ('Unexercised transfer targets', sorted(targets - visited))
report = dict(passed=True, cases=cases, entries=len(entries), continuation_entries=len(meta['modules']['engine']['new_entries']) + len(meta['modules']['qol']['new_entries']),
              visited_targets=sorted(visited), metadata_sha256=sha(out / 'result.json'),
              binary_sha256=sha(test_out / 'test.dylib'), script_sha256=sha(pathlib.Path(__file__).resolve()),
              patched_engine=patched_identity,
              helper_sources_sha256={str(p.relative_to(R)): sha(p) for p in sources},
              scope='Linked captured trampoline prefixes and newly exposed original continuation blocks execute to every existing-block transfer boundary, compared with original x86. GPRs, defined flags, full stack/data region and unrelated CPU state checked. Existing downstream blocks are observers; no patched-entry publication or complete game startup claim.')
if a.patched_engine:
    report['scope'] = 'Complete LLVM functions extracted unchanged from the compiled patched engine input: all 14 entry detours, all 15 changed CALLs with their surrounding basic blocks, and trampoline continuations. Linked bank and DLL continuation execute to every downstream boundary, compared with patched x86. Full engine object compiled separately; this test does not enter whole-engine startup, apply runtime publication or execute downstream observers.'
(test_out / 'test-result.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS:', cases, 'combined native/x86 cases;', len(visited), 'downstream boundaries')
