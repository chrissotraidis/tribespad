#!/usr/bin/env python3
"""Freeze the source-bound CMP repair and its next native startup boundary."""
import datetime
import hashlib
import json
import pathlib
import subprocess

R = pathlib.Path(__file__).resolve().parents[1]
candidate = R / 'generated/integration-review/highbyte-compare-repair'
out = R / 'generated/integration-review/highbyte-compare-repair-checkpoint-20261003'
assert not (out / 'checkpoint.json').exists()

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for data in iter(lambda: f.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()

files = []
seen = set()
def add(src, relative):
    if relative in seen:
        return
    dst = out / relative
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists():
        subprocess.run(['cp', '-c', str(src), str(dst)], check=True)
    assert sha(src) == sha(dst), relative
    seen.add(relative)
    files.append(dict(original=str(src.relative_to(R)), path=relative, sha256=sha(dst)))

def tree(src, relative):
    for path in sorted(src.rglob('*')):
        if path.is_file():
            add(path, str(pathlib.Path(relative) / path.relative_to(src)))

test = json.loads((candidate / 'test/result.json').read_text())
assert test['passed'] and test['captured_native_case']
assert test['exhaustive_cases'] == 2424832 and test['x86_cases'] == 9472
assert test['boundary_reads'] == 74 and len(test['negative']) == 6
for name, expected in test['sources_sha256'].items():
    assert sha(R / name) == expected, name
    add(R / name, 'sources/' + name)
for name, expected in test['outputs_sha256'].items():
    assert sha(candidate / 'test' / name) == expected, name
audit = json.loads((candidate / 'dll-audit.json').read_text())
assert audit['passed'] and audit['test_sha256'] == sha(candidate / 'test/result.json')
assert audit['all_other_module_text_identical'] and audit['tested_body_identical']
assert audit['changed_definitions'] == ['loc_10115878']
native = R / audit['new_native']
assert sha(native / 'module.ll') == audit['new_module_sha256']
assert sha(native / 'module.o') == audit['new_object_sha256']
assert sha(native / 'result.json') == audit['new_preparation_sha256']
assert sha(R / 'scripts/audit-highbyte-dll.py') == audit['script_sha256']
build = R / 'generated/tools/SRW-tribes'
identity = json.loads((build / 'tribes-build.json').read_text())
assert sha(build / 'SRW.exe') == identity['srw_sha256'] == test['srw_sha256']
assert sha(R / 'scripts/patches/srw-tribes.patch') == identity['patch_sha256'] == test['patch_sha256']
runroot = native / 'checked/bootstrap-published-ntdll-probe'
run = json.loads((runroot / 'result.json').read_text())
assert run['state'] == 'finished'
assert run['console_animation_publication'] and not run['playable']
assert run['exit'] == -6 and run['result'] is None
assert 'Returned original DLL stage 1 EAX=1\n' in run['stderr']
assert 'Entering original patched engine at 0x006fde60\n' in run['stderr']
stop = 'TribesPad: unsupported CreateFile access/security/creation/flags/template'
assert run['stderr'].rstrip().endswith(stop)
assert sha(runroot / 'bootstrap') == run['binary_sha256']
assert sha(runroot / 'guarded/result.json') == run['external_build_sha256']
assert sha(native / 'result.json') == run['preparation_sha256']
for name, expected in run['source_sha256'].items():
    assert sha(R / name) == expected, name
    add(R / name, 'sources/' + name)
for name, row in run.get('instruction_checkpoint', {}).items():
    assert sha(R / row['path']) == row['sha256'], name
    add(R / row['path'], 'continuation/capture/' + name)
for name in ['bootstrap', 'result.json', 'stdout.log', 'stderr.log', 'patch-inputs.json',
             'support/result.json', 'guarded/result.json', 'guarded/policy.json']:
    add(runroot / name, 'continuation/' + name)
for path in sorted(runroot.glob('*.ll')):
    add(path, 'continuation/llvm/' + path.name)
for name in ['module.ll', 'module.o', 'dispatch.ll', 'result.json', 'checked/result.json']:
    add(native / name, 'module/' + name)
add(native.parent / 'result.json', 'lift/result.json')
for name in ['SRW.exe', 'tribes-build.json', 'SR_full_llasm_instr.c']:
    add(build / name, 'compiler/' + name)
for name in ['scripts/audit-highbyte-dll.py', 'scripts/freeze-highbyte-compare-repair.py',
             'scripts/build-tribes-lifter.py', 'scripts/update-lifter-patch.py',
             'scripts/prepare-qol-native-diagnostic.py', 'scripts/prepare-checked-qol.py',
             'toolchains.lock.json']:
    add(R / name, 'sources/' + name)
tree(candidate, 'candidate')
for name in ['highbyte-compare-checkpoint-20261003', 'console-animation-publication-20261003']:
    dependency = R / 'generated/integration-review' / name / 'checkpoint.json'
    if name == 'highbyte-compare-checkpoint-20261003':
        assert sha(dependency) == test['capture_checkpoint_sha256']
    add(dependency, 'dependencies/' + name + '.json')
report = dict(passed=True, created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
              files=files, changed_definitions=audit['changed_definitions'],
              exhaustive_cases=test['exhaustive_cases'], x86_cases=test['x86_cases'],
              continuation_exit=run['exit'], continuation_result=run['result'],
              observed_terminal_diagnostic=stop,
              report_parser_limitation='The bootstrap driver does not yet classify this CreateFile abort; its initialized/engine_entered fields are false despite explicit successful attach and engine-entry log records. The run and driver failure are retained without relabeling them as a passing integration test.',
              scope='Strict memory/high-byte CMP lowering, exhaustive ARM64 byte arithmetic and original-x86 comparisons, actual captured input, exact one-block full DLL diff, and guarded startup continuation. Diagnostic traps and integration gaps remain; no original menu, rendering, gameplay or authenticated-match acceptance.')
(out / 'checkpoint.json').write_text(json.dumps(report, indent=2) + '\n')
for row in files:
    assert sha(out / row['path']) == row['sha256']
print('PASS: frozen and verified', len(files), 'files; high-byte CMP repair and native continuation')
