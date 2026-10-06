#!/usr/bin/env python3
"""Compare native guest-buffer checksums with original IFC22 execution evidence."""
import argparse
import hashlib
import json
import pathlib
import resource
import subprocess

R = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument('--simulator-device')
a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
platform = 'simulator' if a.simulator_device else 'macos'
out = R / 'generated/zlib-ng' / platform / ('test-' + (a.simulator_device or 'macos'))
out.mkdir(parents=True, exist_ok=True)
(out / 'result.json').write_text(json.dumps({'pass': False, 'state': 'running'}) + '\n')
if a.simulator_device:
    inventory = json.loads(subprocess.check_output(
        ['xcrun', 'simctl', 'list', 'devices', 'booted', '--json'], text=True))
    assert [d['udid'] for rows in inventory['devices'].values() for d in rows
            if d['state'] == 'Booted'] == [a.simulator_device]
reference_path = R / 'generated/qol-feature-dispatch/result.json'
reference = json.loads(reference_path.read_text())
assert reference['pass']
assert reference['image_sha256'] == sha(R / 'ref/product/IFC22.dll')
assert reference['script_sha256'] == sha(R / 'scripts/test-qol-feature-dispatch.py')
for name, h in reference['native_source_sha256'].items():
    assert sha(R / name) == h
cases = next(p for p in reference['profiles'] if p['name'] == 'native')['checksum_cases']
assert len(cases) == 160
(out / 'cases.inc').write_text(
    'struct checksum_case {uint32_t length,seed,copy,result;};\n'
    'static const struct checksum_case cases[]={\n' + ''.join(
        '{%du,%du,%du,%du},\n' % (v['length'], v['seed'], v['copy'], v['result'])
        for v in cases) + '};\n')
build = R / 'generated/zlib-ng' / platform / 'build'
manifest = json.loads((build.parent / 'result.json').read_text())
lib = build / 'libz-ng.a'
assert manifest['pass'] and manifest['library_sha256'] == sha(lib)
for name, h in manifest['source_sha256'].items():
    assert sha(R / 'ref/zlib-ng' / name) == h
sdkname = 'iphonesimulator' if a.simulator_device else 'macosx'
target = 'arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'
sdk = subprocess.check_output(['xcrun', '--sdk', sdkname, '--show-sdk-path'], text=True).strip()
sources = [R / 'port/core/guest_zlib.c', R / 'tests/guest_zlib_test.c']
exe = out / 'native'
command = ['clang', '-target', target, '-isysroot', sdk, '-O2', '-DPTROFS_64BIT', '-Wall', '-Wextra',
           '-Werror', '-I', str(build), '-I', str(R / 'ref/zlib-ng'),
           '-I', str(R / 'port/core'), '-I', str(out)] + list(map(str, sources)) + [str(lib), '-o', str(exe)]
subprocess.run(command, check=True)
run_command = ['xcrun', 'simctl', 'spawn', a.simulator_device, str(exe)] if a.simulator_device else [str(exe)]
run = subprocess.run(run_command, capture_output=True, text=True, timeout=120)
assert run.returncode == 0 and run.stdout.startswith('PASS:') and not run.stderr, (run.stdout, run.stderr)
(out / 'run.log').write_text(run.stdout)
negative = []
if not a.simulator_device:
    def no_core():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    for name in ['guest-overflow', 'host-overflow', 'null', 'overlap']:
        bad = subprocess.run([str(exe), name], capture_output=True, text=True, preexec_fn=no_core)
        assert bad.returncode == -6 and 'TribesPad:' in bad.stderr, (name, bad)
        negative.append({'name': name, 'exit': bad.returncode, 'stderr': bad.stderr})
report = {'pass': True, 'state': 'finished', 'platform': platform,
          'simulator_device': a.simulator_device, 'cases': 2560,
          'reference_sha256': sha(reference_path), 'library_sha256': sha(lib),
          'source_sha256': {str(p.relative_to(R)): sha(p) for p in sources + [
              R / 'port/core/guest_zlib.h', pathlib.Path(__file__).resolve()]},
          'fixture_sha256': sha(out / 'cases.inc'), 'binary_sha256': sha(exe),
          'compile_command': command, 'output': run.stdout.strip(), 'negative': negative,
          'scope': 'Buffer-only native adapter: 160 original x86 checksum/copy results at16 alignments, entire arena comparison. No guest stream ABI, module routing or full client execution; four strict range/overlap failures on macOS only.'}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(run.stdout.strip())
