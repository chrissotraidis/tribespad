#!/usr/bin/env python3
"""Account for the captured HTTP patch edits without enabling native writes."""
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
assert trace['stop'] == {'kind': 'patch_routine_return', 'pc': 0x10061031}
assert trace['script_sha256'] == sha(R / 'scripts/trace-http-qol-patches.py')
checkpoint_path = R / trace['checkpoint_path']
assert sha(checkpoint_path) == trace['checkpoint_sha256']
checkpoint = json.loads(checkpoint_path.read_text())
inputs = checkpoint['http_patch_checkpoint']
assert inputs == trace['checkpoint_inputs']
for item in inputs.values():
    path = R / item['path']
    assert path.stat().st_size == item['bytes'] and sha(path) == item['sha256']
pages = json.loads((R / inputs['http-patch-pages.json']['path']).read_text())['ranges']
before = {row['address']: (R / inputs[row['file']]['path']).read_bytes() for row in pages}
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
assert len(engine) == 17
assert len(trace['code_writes']) == 59 and len(trace['image_data_writes']) == 28
edits = []
for event in engine:
    address = event['address']
    if address in (0x5bf5d2, 0x5bf5d6, 0x44aca0, 0x44aca8, 0x44aca5):
        continue
    assert event['size'] == 4
    pc = address - 1
    old, = instructions(before, pc, 5)
    new, = instructions(after, pc, 5)
    if pc == 0x5bdfc3:
        assert old.mnemonic == new.mnemonic == 'push'
        assert old.operands[0].imm == 0x84 and new.operands[0].imm == 0x1c0
    else:
        assert old.mnemonic == new.mnemonic == 'call'
        assert new.operands[0].imm == 0x436c20 or 0x10000000 <= new.operands[0].imm < 0x10129000
    edits.append(dict(pc=pc, old=old.mnemonic + ' ' + old.op_str,
                      new=new.mnemonic + ' ' + new.op_str, writer=event['pc']))
old, = instructions(before, 0x5bf5d2, 5)
assert old.mnemonic == 'call' and old.operands[0].imm == 0x5bddb0
assert read(after, 0x5bf5d2, 5) == b'\x90' * 5
assert read(before, 0x44aca0, 16).hex() == '8d85f0fcffff8b15187975005250e8ad'
replacement = instructions(after, 0x44aca0, 16)
assert [(i.mnemonic, i.op_str) for i in replacement] == [
    ('mov', 'eax, dword ptr [ebx + 0x14]'), ('push', 'eax'),
    ('call', '0x100238c0'), ('mov', 'esi, eax'), ('jmp', '0x44ad48')]
# The replacement ends inside the original CALL. Its unconditional jump
# skips the surviving tail; those bytes must not become a fallthrough route.
original_span = instructions(before, 0x44aca0, 19)
assert original_span[-1].address == 0x44acae and original_span[-1].size == 5
assert read(before, 0x44acb0, 3) == read(after, 0x44acb0, 3)

bank = 0x3c0000
assert struct.unpack('<I', read(before, bank + 8, 4))[0] == bank + 0x8b8
assert struct.unpack('<I', read(after, bank + 8, 4))[0] == bank + 0x900
prior_paths = [R / 'generated/integration-review/native-patch-current.json',
               R / 'generated/integration-review/followup-bank-plan.json',
               R / 'generated/integration-review/adapters-startup-checkpoint-20261002/network-patch-audit.json']
prior = []
for path in prior_paths:
    plan = json.loads(path.read_text())
    assert plan['verified']
    prior.extend(plan['trampolines'])
assert len(prior) == 26 and len({row['address'] for row in prior}) == 26
for record in prior:
    code = bytes.fromhex(record['code_hex'])
    assert read(before, record['address'], len(code)) == code == read(after, record['address'], len(code))
address = bank + 0x8b8
block = read(after, address, 72)
assert block[30] == block[54] == 5
continuation, detour = struct.unpack_from('<II', block, 64)
assert (continuation, detour) == (0x43fcc5, 0x100229c0)
target = continuation - 5
saved = read(before, target, 5)
assert saved == block[32:37] == read(after, target, 5)
relocated = instructions(after, address, 10)
original = instructions(before, target, 5)
assert len(relocated) == len(original) + 1
for left, right in zip(original, relocated[:-1]):
    assert not left.group(capstone.CS_GRP_JUMP) and not left.group(capstone.CS_GRP_CALL)
    assert bytes(left.bytes) == bytes(right.bytes)
assert relocated[-1].mnemonic == 'jmp' and relocated[-1].operands[0].imm == continuation
report = dict(
    verified=True, native_activation=False, trace_path=str(trace_path.relative_to(R)),
    trace_sha256=sha(trace_path), checkpoint_sha256=sha(checkpoint_path),
    script_sha256=sha(pathlib.Path(__file__).resolve()),
    prior_plans_sha256={str(path.relative_to(R)): sha(path) for path in prior_paths},
    engine_writes=engine, instruction_edits=edits,
    nop_fill={'address': 0x5bf5d2, 'size': 5},
    block_replacement={'address': 0x44aca0, 'size': 16, 'old_instruction_span': 19,
                       'call_continuation': 0x44aca9, 'exit': 0x44ad48,
                       'removed_absolute_fixup': 0x44aca8,
                       'instructions': [{'pc': i.address, 'text': i.mnemonic + ' ' + i.op_str}
                                        for i in replacement]},
    trampolines=[dict(address=address, original_target=target, continuation=continuation,
                      detour_target=detour, code_hex=block[:10].hex(), restore_hex=saved.hex())],
    prior_trampoline_count=len(prior),
    scope='All recorded image/bank writes replay and account for captured changes. '
          'Seventeen engine stores include a 16-byte replacement and overlapping CALL edit; '
          'one new trampoline is registered, with 26 prior instruction prefixes unchanged. '
          'No strict compiler candidate, native publication or numeric differential acceptance.')
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(json.dumps(report, indent=2) + '\n')
print('PASS: 17 engine stores; 16-byte block replacement; 1 new and 26 unchanged trampoline prefixes')
