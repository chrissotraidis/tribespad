#!/usr/bin/env python3
"""Account for the captured string/utility patch edits without enabling native writes."""
import argparse
import hashlib
import json
import pathlib
import struct

import capstone

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
parser = argparse.ArgumentParser()
parser.add_argument('trace', type=pathlib.Path)
parser.add_argument('--output', type=pathlib.Path, required=True)
args = parser.parse_args()
trace_path = args.trace.resolve()
trace = json.loads(trace_path.read_text())
assert trace['completed_patch_routine'] and not trace['unconfirmed_writes']
assert trace['stop'] == {'kind': 'patch_routine_return', 'pc': 0x10061036}
assert trace['script_sha256'] == sha(R / 'scripts/trace-string-qol-patches.py')
checkpoint_path = R / trace['checkpoint_path']
assert sha(checkpoint_path) == trace['checkpoint_sha256']
checkpoint = json.loads(checkpoint_path.read_text())
inputs = checkpoint['http_patch_final']
assert inputs == trace['checkpoint_inputs']
for item in inputs.values():
    path = checkpoint_path.parent / pathlib.Path(item['path']).name
    assert path.stat().st_size == item['bytes'] and sha(path) == item['sha256']
pages = json.loads((checkpoint_path.parent / 'http-final-pages.json').read_text())['ranges']
before = {row['address']: (checkpoint_path.parent / row['file']).read_bytes() for row in pages}
after = {address: bytearray(data) for address, data in before.items()}


def region(address, size):
    found = [row for row in pages
             if row['address'] <= address and address + size <= row['address'] + row['size']]
    assert len(found) == 1, (hex(address), size)
    return found[0]


def read(table, address, size):
    row = region(address, size)
    offset = address - row['address']
    return bytes(table[row['address']][offset:offset + size])


# The two categories occupy disjoint ranges. Keep original ordering inside
# each category, including the overlapping second edit of the new CALL.
for event in trace['code_writes'] + trace['image_data_writes']:
    assert event['completed']
    assert read(after, event['address'], event['size']).hex() == event['before']
    data = bytes.fromhex(event['after'])
    assert len(data) == event['size']
    row = region(event['address'], event['size'])
    offset = event['address'] - row['address']
    after[row['address']][offset:offset + len(data)] = data
changes = {row['address']: row for row in trace['changed_regions']}
for row in pages:
    address = row['address']
    if not (0x3c0000 <= address < 0x3d0000 or 0x400000 <= address < 0xa00000
            or 0x10000000 <= address < 0x10500000):
        continue
    final = before[address]
    if address in changes:
        item = changes[address]
        path = R / item['after_path']
        assert sha(path) == item['after_sha256']
        final = path.read_bytes()
    assert bytes(after[address]) == final, ('unrecorded image/bank mutation', hex(address))

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True


def instructions(table, address, size):
    code = read(table, address, size)
    result = list(md.disasm(code, address))
    assert sum(ins.size for ins in result) == size, ('partial instruction', hex(address))
    return result


engine = [row for row in trace['code_writes'] if row['image'] == 'engine']
assert len(engine) == 114
assert len(trace['code_writes']) == 238 and len(trace['image_data_writes']) == 33
bank = 0x3c0000
assert struct.unpack('<I', read(before, bank + 8, 4))[0] == bank + 0x900
assert struct.unpack('<I', read(after, bank + 8, 4))[0] == bank + 0x9d8
prior_paths = [R / 'generated/integration-review/native-patch-current.json',
               R / 'generated/integration-review/followup-bank-plan.json',
               R / 'generated/integration-review/adapters-startup-checkpoint-20261002/network-patch-audit.json',
               R / 'generated/integration-review/curl-version-checkpoint-20261002/http-patch-audit.json']
prior = []
for path in prior_paths:
    plan = json.loads(path.read_text())
    assert plan['verified']
    prior.extend(plan['trampolines'])
assert len(prior) == 27 and len({row['address'] for row in prior}) == 27
for record in prior:
    code = bytes.fromhex(record['code_hex'])
    assert read(before, record['address'], len(code)) == code == read(after, record['address'], len(code))
trampolines = []
for address, target, continuation, detour in [
        (bank + 0x900, 0x44d390, 0x44d396, 0x100118c0),
        (bank + 0x948, 0x5b11e0, 0x5b11e5, 0x10010e10),
        (bank + 0x990, 0x58fa80, 0x58fa86, 0x10010fb0)]:
    block = read(after, address, 72)
    moved = continuation - target
    assert block[30] == block[54] == moved
    assert struct.unpack_from('<II', block, 64) == (continuation, detour)
    saved = read(before, target, moved)
    assert saved == block[32:32 + moved] == read(after, target, moved)
    relocated = instructions(after, address, moved + 5)
    original = instructions(before, target, moved)
    assert len(relocated) == len(original) + 1
    for left, right in zip(original, relocated[:-1]):
        assert not left.group(capstone.CS_GRP_JUMP) and not left.group(capstone.CS_GRP_CALL)
        assert bytes(left.bytes) == bytes(right.bytes)
    assert relocated[-1].mnemonic == 'jmp' and relocated[-1].operands[0].imm == continuation
    trampolines.append(dict(address=address, original_target=target, continuation=continuation,
                           detour_target=detour, code_hex=block[:moved + 5].hex(), restore_hex=saved.hex()))
report = dict(
    verified=True, native_activation=False, trace_path=str(trace_path.relative_to(R)),
    trace_sha256=sha(trace_path), checkpoint_sha256=sha(checkpoint_path),
    script_sha256=sha(pathlib.Path(__file__).resolve()),
    prior_plans_sha256={str(path.relative_to(R)): sha(path) for path in prior_paths},
    engine_writes=engine, trampolines=trampolines, prior_trampoline_count=len(prior),
    scope='All 271 recorded image/bank writes replay and account for captured changes. '
          '114 engine stores and three new prologue records; 27 prior trampoline instruction '
          'prefixes remain unchanged. Semantic instruction/relocation review, strict lifting, '
          'differential tests and native activation remain required.')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2) + '\n')
print('PASS: complete image/bank byte accounting; 114 engine writes; 3 new and 27 unchanged trampoline prefixes')
