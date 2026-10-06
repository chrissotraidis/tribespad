#!/usr/bin/env python3
"""Derive one finite relative-CALL AOT variant; do not activate it."""
import hashlib, json, pathlib, re, struct
import capstone
from console_fade_inputs import R, FROZEN, OUT, sha, load, definition

read, pages, engine, dll = load()
OUT.mkdir(parents=True, exist_ok=True)
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
decoder.detail = True
original = read(0x5d2842, 0x22)
before = list(decoder.disasm(original, 0x5d2842))
assert before[-1].address == 0x5d285f and before[-1].mnemonic == 'call'
assert before[-1].size == 5 and before[-1].operands[0].imm == 0x5de240
patched = original[:-4] + struct.pack('<I', (0x10002550 - 0x5d2864) & 0xffffffff)
after = list(decoder.disasm(patched, 0x5d2842))
assert len(before) == len(after) == 8
assert [(i.address, i.bytes) for i in before[:-1]] == [(i.address, i.bytes) for i in after[:-1]]
assert after[-1].size == 5 and after[-1].operands[0].imm == 0x10002550
engine_text = engine.read_text()
old = definition(engine_text, 'loc_5D2842')
assert old.count('@loc_5DE240(') == 1 and old.count(';tcall loc_5DE240') == 1
new = old.replace('@loc_5DE240(', '@loc_10002550(').replace(';tcall loc_5DE240', ';tcall loc_10002550')
preamble = engine_text[:engine_text.index('define ')]
assert '@loc_10002550(' not in preamble
(OUT / 'engine.ll').write_text(preamble + 'declare hidden fastcc void @loc_10002550(%_cpu*)\n' + new)
text = dll.read_text()
selected = {}; pending = ['loc_10002550']
while pending:
    name = pending.pop()
    if name in selected: continue
    assert 0x10002550 <= int(name[4:], 16) < 0x1000268c
    body = definition(text, name); selected[name] = body
    pending.extend(set(re.findall(r'@(loc_[0-9A-F]+)\(', body)) - set(selected))
assert len(selected) == 20
(OUT / 'fade.ll').write_text(text[:text.index('define ')] + '\n'.join(selected[n] for n in sorted(selected)))
report = dict(passed=True, published=False, engine_entry='loc_5D2842', target='loc_10002550',
    before_bytes=original.hex(), after_bytes=patched.hex(),
    write=dict(pc=0x10002936, address=0x5d2860, size=4, before=int.from_bytes(original[-4:], 'little'), value=int.from_bytes(patched[-4:], 'little')),
    old_definition_sha256=hashlib.sha256(old.encode()).hexdigest(),
    new_definition_sha256=hashlib.sha256(new.encode()).hexdigest(),
    fade_definitions={n: hashlib.sha256(b.encode()).hexdigest() for n,b in selected.items()},
    inputs_sha256={str(p.relative_to(R)): sha(p) for p in [engine, dll, FROZEN/'checkpoint.json', pathlib.Path(__file__).resolve(), R/'scripts/console_fade_inputs.py']},
    outputs_sha256={n: sha(OUT/n) for n in ['engine.ll', 'fade.ll']},
    scope='One complete engine block, changing only its final direct CALL target, and the original 20-block fade routine. No runtime publication or complete console initializer claim.')
(OUT / 'preparation.json').write_text(json.dumps(report, indent=2) + '\n')
print('Prepared unpublished CALL variant and 20-block original fade routine')
