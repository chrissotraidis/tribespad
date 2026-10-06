#!/usr/bin/env python3
"""Report source-bound translation gaps; never certify execution or gameplay."""
import argparse
import collections
import hashlib
import json
import pathlib
import re

import capstone
import pefile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('lift', type=pathlib.Path)
parser.add_argument('--output', type=pathlib.Path,
                    default=ROOT / 'generated/integration-review/lifter-gaps.json')
args = parser.parse_args()
work = args.lift.resolve()
result = json.loads((work / 'result.json').read_text())
assert result['diagnostic'], 'A first-error strict run is not a gap census'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
images = list(work.glob('*.dll')) + list(work.glob('*.exe'))
assert len(images) == 1
image = images[0]
assert sha(image) == result['image_sha256']
pe = pefile.PE(str(image))
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
log = (work / 'stderr.log').read_text()
sites = {}
for kind, address in re.findall(r'HALOPAD-DIAG ([\w-]+) (0x[0-9a-f]+)', log):
    pc = int(address, 16)
    row = sites.setdefault(pc, {'pc': address, 'diagnostics': []})
    if kind not in row['diagnostics']:
        row['diagnostics'].append(kind)
    raw = pe.get_data(pc - pe.OPTIONAL_HEADER.ImageBase, 15)
    instruction = next(decoder.disasm(raw, pc, count=1), None)
    if instruction:
        row.update(bytes_hex=instruction.bytes.hex(), mnemonic=instruction.mnemonic,
                   operands=instruction.op_str)
    else:
        row.update(bytes_hex=raw.hex(), mnemonic=None, operands=None)
structural = [line for line in log.splitlines()
              if any(message in line for message in
                     ['Error: wrong disassembly', 'Error: fixup operand mismatch'])]
families = collections.Counter(row['mnemonic'] for row in sites.values())
args.output.parent.mkdir(parents=True, exist_ok=True)
report = {
    'schema': 1,
    'lift': str(work.relative_to(ROOT)),
    'translator_exit': result['exit'],
    'census_finished': result['exit'] == 0,
    'all_code_coverage_proven': False,
    'playable': False,
    'unique_diagnostic_sites': len(sites),
    'families': dict(sorted(families.items(), key=lambda item: str(item[0]))),
    'structural_errors': structural,
    'sites': [sites[pc] for pc in sorted(sites)],
    'source_sha256': {
        str(p.relative_to(ROOT)): sha(p) for p in
        [pathlib.Path(__file__).resolve(), work / 'result.json',
         work / 'stderr.log', image]
    },
    'srw_sha256': result['srw_sha256'],
    'scope': 'Observed diagnostic traversal only. Families are independently decoded '
             'from pinned PE bytes, but a diagnostic address is not proof of a valid '
             'instruction start: code/data classification must be checked. Nonzero '
             'exit leaves the remaining image uncensused. Even a finished diagnostic '
             'contains traps and cannot establish strict translation or game execution.'
}
args.output.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({key: report[key] for key in
                 ['lift', 'translator_exit', 'census_finished',
                  'unique_diagnostic_sites', 'families', 'structural_errors']}, indent=2))
