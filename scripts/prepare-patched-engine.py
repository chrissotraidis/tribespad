#!/usr/bin/env python3
"""Strictly lift the verified final engine patches, including continuations.

The extra PE section represents external DLL symbols for the static translator
only. This candidate must never be loaded as a guest image or published before
runtime patch validation. The host continues loading the pinned original PE.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import shutil
import struct
import subprocess

import pefile

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('original_lift', type=pathlib.Path)
p.add_argument('--prepare-native', action='store_true')
a = p.parse_args()
source = a.original_lift.resolve()
plan_path = R / 'generated/integration-review/native-patch-current.json'
native = json.loads(plan_path.read_text())
assert native['verified'] and native['all_reference_engine_changes_observed']
assert sha(R / native['capture']) == native['capture_sha256']
reference_path = R / native['reference']
assert sha(reference_path) == native['reference_sha256']
plan = json.loads(reference_path.read_text())
original = source / 'Tribes2.exe'
identity = json.loads((source / 'result.json').read_text())
assert identity['exit'] == 0 and identity['full_flags'] and not identity['diagnostic']
assert sha(original) == identity['image_sha256'] == plan['inputs_sha256']['engine']
srw = R / 'generated/tools/SRW-tribes/SRW.exe'
build = json.loads(srw.with_name('tribes-build.json').read_text())
assert sha(srw) == build['srw_sha256'] == identity['srw_sha256']
assert sha(R / 'scripts/patches/srw-tribes.patch') == build['patch_sha256']
out = R / 'generated/integration-review/patched-engine'
out.mkdir(exist_ok=True)
(out / 'result.json').write_text(json.dumps(dict(state='preparing', published=False)) + '\n')
raw = bytearray(original.read_bytes())
pe = pefile.PE(data=bytes(raw))
base = pe.OPTIONAL_HEADER.ImageBase
for row in plan['engine_regions']:
    offset = pe.get_offset_from_rva(row['address'] - base)
    before, after = bytes.fromhex(row['before_hex']), bytes.fromhex(row['after_hex'])
    assert len(before) == len(after) == row['size']
    assert raw[offset:offset + len(before)] == before
    raw[offset:offset + len(after)] = after
targets = sorted({r['detour_target'] for r in plan['entry_redirects']} | {r['new_target'] for r in plan['code_calls']})
qol = pefile.PE(str(R / 'ref/product/IFC22.dll'))
assert sha(R / 'ref/product/IFC22.dll') == plan['inputs_sha256']['qol']
assert all(qol.OPTIONAL_HEADER.ImageBase <= v < qol.OPTIONAL_HEADER.ImageBase + qol.OPTIONAL_HEADER.SizeOfImage for v in targets)
header = pe.DOS_HEADER.e_lfanew
section = header + 24 + pe.FILE_HEADER.SizeOfOptionalHeader + 40 * pe.FILE_HEADER.NumberOfSections
assert section + 40 <= pe.OPTIONAL_HEADER.SizeOfHeaders
assert not any(raw[section:section + 40])
struct.pack_into('<8s6I2HI', raw, section, b'.qolref', qol.OPTIONAL_HEADER.SizeOfImage,
                 qol.OPTIONAL_HEADER.ImageBase - base, 0, 0, 0, 0, 0, 0, 0x40000040)
struct.pack_into('<H', raw, header + 6, pe.FILE_HEADER.NumberOfSections + 1)
# Preserve the original PE relocation mode and its reviewed literal exceptions.
# Stale relocation entries inside NOP fills are disabled below.
struct.pack_into('<I', raw, header + 24 + 56, qol.OPTIONAL_HEADER.ImageBase + qol.OPTIONAL_HEADER.SizeOfImage - base)
relocations = []
adjustments = []
for line in (source / 'relocations.csv').read_text().splitlines():
    address, target = [int(s, 0) for s in line.split(',')[:2]]
    touched = any(address < r['address'] + r['size'] and address + 4 > r['address'] for r in plan['engine_regions'])
    if touched:
        offset = pe.get_offset_from_rva(address - base)
        after = bytes(raw[offset:offset + 4])
        if after == b'\x90' * 4:
            adjustments.append(dict(address=address, old_target=target, action='removed_from_NOP_fill'))
            continue
        assert address == 0x5f30db and target == 0x79eb5c and after == bytes.fromhex('60e17900')
        adjustments.append(dict(address=address, old_target=target, new_target=0x79e160, action='updated_data_pointer'))
        target = 0x79e160
    relocations.append('0x%x,0x%x,\n' % (address, target))
assert len(adjustments) == 15
removed = {r['address'] for r in adjustments if r['action'] == 'removed_from_NOP_fill'}
disabled = set()
for block in pe.DIRECTORY_ENTRY_BASERELOC:
    for entry in block.entries:
        if base + entry.rva in removed:
            assert entry.type == 3
            # IMAGE_REL_BASED_ABSOLUTE is padding; keep the directory layout.
            struct.pack_into('<H', raw, entry.struct.get_file_offset(), entry.rva & 0xfff)
            disabled.add(base + entry.rva)
assert disabled == removed
(out / 'Tribes2.exe').write_bytes(raw)
(out / 'relocations.csv').write_text(''.join(relocations))
assert not (source / 'global_aliases.sci').exists() and not (source / 'external_procedures.sci').exists()
for path in source.glob('*.sci'): shutil.copy2(path, out / path.name)
if (source / 'literal_fixups.csv').exists(): shutil.copy2(source / 'literal_fixups.csv', out / 'literal_fixups.csv')
continuations = sorted(r['continuation'] for r in native['trampolines'] if r['original_image'] == 'engine')
(out / 'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n' % (v, v) for v in continuations))
(out / 'external_procedures.sci').write_text(''.join('loc_%X,loc_%X\n' % (v, v) for v in targets))
env = {k: v for k, v in os.environ.items() if k != 'HALOPAD_SRW_DIAG'}
env['TRIBES_SRW_FULL_FLAGS'] = '1'
with (out / 'srw.log').open('w') as log:
    run = subprocess.run([str(srw), 'Tribes2.exe', 'tribes2.llasm'], cwd=out, env=env,
                         stdout=log, stderr=log, timeout=600)
assert run.returncode == 0, out / 'srw.log'
code = (out / 'seg01_code.llinc').read_text()
for row in plan['entry_redirects']:
    assert re.search(r'^proc loc_%X(?:[^\n]*)\n;jmp loc_%X\ntcall loc_%X\nendp' % (row['entry'], row['detour_target'], row['detour_target']), code, re.M), row
for row in plan['code_calls']:
    assert 'PUSH loc_%X\ntcall loc_%X\n' % (row['call_site'] + 5, row['new_target']) in code, row
assert all(re.search(r'^proc loc_%X\b' % v, code, re.M) for v in continuations)
assert 'halopad_trap_unimplemented' not in code
main = (out / 'tribes2.llasm').read_text()
includes = ''.join('include %s\n' % n for n in ['tribes_mmx.llinc', 'tribes_sse.llinc', 'tribes_scan.llinc', 'tribes_x87.llinc', 'tribes_shift.llinc', 'tribes_atomic.llinc'])
(out / 'tribes2.llasm').write_text(includes + main)
known = set(re.findall(r'^proc (\S+) external', main, re.M))
imports = {i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
(out / 'extern.llinc').write_text(''.join('proc %s external\n' % n for n in sorted(imports - known)) +
                                ''.join('proc loc_%X external\n' % v for v in targets))
(out / 'macros.llinc').write_text('')
report = dict(state='strictly_lifted_unpublished', exit=0, diagnostic=False, full_flags=True,
              image_sha256=sha(out / 'Tribes2.exe'), original_image_sha256=sha(original),
              srw_sha256=sha(srw), patch_candidate=True, guest_loadable=False, published=False,
              native_plan_sha256=sha(plan_path), reference_sha256=sha(reference_path),
              external_targets=targets, continuations=continuations, relocation_adjustments=adjustments,
              script_sha256=sha(pathlib.Path(__file__).resolve()),
              code_sha256=sha(out / 'seg01_code.llinc'),
              scope='Compiler-only final patched engine variant. All 14 entry redirects and 15 patched CALLs present, with continuation splits. No runtime publication or dynamic patch validation.')
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
if a.prepare_native:
    subprocess.run([str(R / '.venv/bin/python'), str(R / 'scripts/prepare-va-model.py'), str(out)], check=True)
print(json.dumps(report, indent=2))
