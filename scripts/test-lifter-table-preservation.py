#!/usr/bin/env python3
"""Verify original DLL jump tables survive region repair in emitted data."""
import argparse
import hashlib
import json
import pathlib
import re
import struct

import pefile

ROOT = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('lift', type=pathlib.Path)
a = ap.parse_args()
work = a.lift.resolve()
result = json.loads((work / 'result.json').read_text())
assert result['exit'] == 0
image = work / 'IFC22.dll'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(image) == result['image_sha256']
pe = pefile.PE(str(image))
audit_path = ROOT / 'generated/analysis/ifc22/audit.json'
audit = json.loads(audit_path.read_text())
assert audit['sha256'] == sha(image)
data_path = work / 'seg01_data.llinc'
data = data_path.read_text()
log = (work / 'stderr.log').read_text()
assert 'Error: wrong disassembly' not in log
assert 'Error: fixup operand mismatch' not in log
checked = []
for address in [0x10074AF0, 0x1007527C, 0x1007607C]:
    table = next(row for row in audit['jump_tables'] if row['table'] == address)
    original = pe.get_data(address - pe.OPTIONAL_HEADER.ImageBase, table['entries'] * 4)
    targets = list(struct.unpack('<' + 'I' * table['entries'], original))
    match = re.search(r'^dlabel loc_%X\n((?:daddr loc_[0-9A-F]+\n)+)' % address, data, re.M)
    assert match, hex(address)
    emitted = [int(value, 16) for value in re.findall(r'daddr loc_([0-9A-F]+)', match[1])]
    assert emitted == targets, (hex(address), emitted, targets)
    for index in range(table['entries']):
        assert not re.search(r'HALOPAD-DIAG [\w-]+ 0x%x\b' % (address + index * 4), log)
    checked.append({'table': hex(address), 'entries': len(targets),
                    'original_bytes_sha256': hashlib.sha256(original).hexdigest(),
                    'targets': [hex(target) for target in targets]})
out = ROOT / 'generated/integration-review/table-preservation.json'
out.write_text(json.dumps({
    'pass': True, 'tables': checked, 'lift': str(work.relative_to(ROOT)),
    'source_sha256': {str(p.relative_to(ROOT)): sha(p) for p in
                      [image, audit_path, data_path, work / 'result.json',
                       work / 'stderr.log', pathlib.Path(__file__).resolve()]},
    'scope': 'All49 entries of the three previously failing tables retain exact '
             'original target order in emitted data, without instruction diagnostics. '
             'This is data preservation, not native dispatch or DLL execution.'
}, indent=2) + '\n')
print('PASS: all49 original jump-table entries preserved in data; no structural errors')
