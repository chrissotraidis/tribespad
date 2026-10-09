#!/usr/bin/env python3
"""Refresh the frozen engine lift with the verified x87 store compiler fix.

Keep historical startup/publication receipts immutable. Relift the same compiler-
only PE, prove that only FST/FSTP result preservation changed, and retain the new
native definitions for integration before the usual guest-memory checker.
"""
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys

R = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
old = R / 'generated/integration-review/patched-engine'
out = R / 'generated/integration-review/x87-store-engine'
out.mkdir(exist_ok=True)
srw = R / 'generated/tools/SRW-tribes/SRW.exe'
build = json.loads(srw.with_name('tribes-build.json').read_text())
assert sha(srw) == build['srw_sha256']
assert sha(R / 'scripts/patches/srw-tribes.patch') == build['patch_sha256']
test_path = R / 'generated/integration-review/x87-indexed-stores/result.json'
test = json.loads(test_path.read_text())
assert test['passed'] and test['srw_sha256'] == sha(srw)
for name, digest in test['source_sha256'].items():
    assert sha(R / name) == digest, name
parent = json.loads((old / 'result.json').read_text())
assert sha(old / 'Tribes2.exe') == parent['image_sha256']
assert sha(old / 'seg01_code.llinc') == parent['code_sha256']
for path in old.iterdir():
    if path.is_file() and (path.suffix in ('.sci', '.csv') or path.name == 'Tribes2.exe'):
        shutil.copy2(path, out / path.name)
env = {k: v for k, v in dict(os.environ, TRIBES_SRW_FULL_FLAGS='1').items() if k != 'HALOPAD_SRW_DIAG'}
with (out / 'srw.log').open('w') as log:
    subprocess.run([str(srw), 'Tribes2.exe', 'tribes2.llasm'], cwd=out, env=env,
                   stdout=log, stderr=log, check=True, timeout=600)
old_code = (old / 'seg01_code.llinc').read_text()
new_code = (out / 'seg01_code.llinc').read_text()
pattern = re.compile(r'^(FSTP?_FLOAT)\nmov tmp1, tmp0\n(.*?)^store tmp1, ([^\n]+)$', re.M | re.S)
stores = []
def previous_store(match):
    # Nothing except effective-address calculation may occur in this interval.
    assert '\n;' not in '\n' + match[2] and 'FST' not in match[2] and 'endp' not in match[2]
    stores.append(match.start())
    return match[1] + '\n' + match[2] + 'store tmp0, ' + match[3]
assert pattern.sub(previous_store, new_code) == old_code
assert len(stores) == 13852
includes = ''.join('include %s\n' % n for n in [
    'tribes_mmx.llinc', 'tribes_sse.llinc', 'tribes_scan.llinc',
    'tribes_x87.llinc', 'tribes_shift.llinc', 'tribes_atomic.llinc'])
main = includes + (out / 'tribes2.llasm').read_text()
assert main == (old / 'tribes2.llasm').read_text()
(out / 'tribes2.llasm').write_text(main)
for name in ['extern.llinc', 'macros.llinc']:
    shutil.copy2(old / name, out / name)
receipt = dict(exit=0, full_flags=True, diagnostic=False,
               image_sha256=sha(out / 'Tribes2.exe'), parent_sha256=sha(old / 'result.json'),
               srw_sha256=sha(srw), preserved_stores=len(stores),
               regression_sha256=sha(test_path), script_sha256=sha(pathlib.Path(__file__).resolve()),
               scope='Fresh strict lift of unchanged compiler-only PE. Not a guest-loadable image.')
(out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
subprocess.run([sys.executable, str(R / 'scripts/prepare-va-model.py'), str(out)], check=True)
