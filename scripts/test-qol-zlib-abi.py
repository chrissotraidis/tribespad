#!/usr/bin/env python3
"""Execute actual finite dispatch and native cdecl checksum wrappers on ARM64."""
import argparse, hashlib, importlib.util, json, pathlib, subprocess
R = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(); ap.add_argument('--simulator-device'); a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
platform = 'simulator' if a.simulator_device else 'macos'
out = R / 'generated/zlib-ng' / platform / ('abi-' + (a.simulator_device or 'macos'))
out.mkdir(parents=True, exist_ok=True)
(out / 'result.json').write_text(json.dumps({'pass': False, 'state': 'running'}) + '\n')
if a.simulator_device:
    inventory = json.loads(subprocess.check_output(
        ['xcrun', 'simctl', 'list', 'devices', 'booted', '--json'], text=True))
    assert [d['udid'] for rows in inventory['devices'].values() for d in rows
            if d['state'] == 'Booted'] == [a.simulator_device]
spec = importlib.util.spec_from_file_location('routes', R / 'scripts/prepare-qol-zlib-routes.py')
routes = importlib.util.module_from_spec(spec); spec.loader.exec_module(routes)
prepared = routes.prepare(out / 'routes')
reference_path = R / 'generated/qol-feature-dispatch/result.json'
null_path = R / 'generated/qol-zlib-null/result.json'
reference = json.loads(reference_path.read_text()); null = json.loads(null_path.read_text())
assert reference['pass'] and null['pass'] and null['image_sha256'] == reference['image_sha256']
assert null['script_sha256'] == sha(R / 'scripts/test-qol-zlib-null.py')
original = next(p for p in reference['profiles'] if p['name'] == 'native')['checksum_cases']
cases = [dict(c, source_null=False, destination_null=False) for c in original]
cases += [c for c in null['cases'] if not c['fault']]
assert len(cases) == 200
(out / 'cases.inc').write_text(
    'struct checksum_case {uint32_t length,seed,copy,result,source_null,destination_null;};\n'
    'static const struct checksum_case cases[]={\n' + ''.join(
        '{%du,%du,%du,%du,%du,%du},\n' % tuple(v[k] for k in
            ['length', 'seed', 'copy', 'result', 'source_null', 'destination_null'])
        for v in cases) + '};\n')
build = R / 'generated/zlib-ng' / platform / 'build'
manifest = json.loads((build.parent / 'result.json').read_text()); lib = build / 'libz-ng.a'
assert manifest['pass'] and manifest['library_sha256'] == sha(lib)
assert manifest['adapter']['source_sha256'] == sha(R / 'port/core/guest_zlib.c')
for name, h in manifest['source_sha256'].items(): assert sha(R / 'ref/zlib-ng' / name) == h
sdkname = 'iphonesimulator' if a.simulator_device else 'macosx'
target = 'arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'
sdk = subprocess.check_output(['xcrun', '--sdk', sdkname, '--show-sdk-path'], text=True).strip()
sources = [R / 'port/core/guest_zlib.c', R / 'port/core/guest_zlib_chunk.c', R / 'port/core/guest_dispatch.c',
           R / 'tests/qol_zlib_abi_test.c', out / 'routes/bindings.ll', out / 'routes/dispatch.ll']
exe = out / 'native'
command = ['clang', '-target', target, '-isysroot', sdk, '-O2', '-Wall', '-Wextra',
           '-Werror', '-Wno-override-module', '-DPTROFS_64BIT', '-I', str(build),
           '-I', str(R / 'ref/zlib-ng'), '-I', str(out)] + list(map(str, sources)) + [str(lib), '-o', str(exe)]
subprocess.run(command, check=True)
run_command = ['xcrun', 'simctl', 'spawn', a.simulator_device, str(exe)] if a.simulator_device else [str(exe)]
run = subprocess.run(run_command, capture_output=True, text=True, timeout=120)
assert run.returncode == 0 and run.stdout.startswith('PASS:') and not run.stderr, (run.returncode, run.stdout, run.stderr)
(out / 'run.log').write_text(run.stdout)
paths = sources + [R / 'port/core/guest_zlib.h', R / 'port/llasm-support/llasm_cpu.h', pathlib.Path(__file__).resolve()]
report = {'pass': True, 'state': 'finished', 'platform': platform, 'simulator_device': a.simulator_device,
          'cases': 3200, 'original_reference_sha256': sha(reference_path), 'null_reference_sha256': sha(null_path),
          'routes': prepared, 'library_sha256': sha(lib), 'binary_sha256': sha(exe),
          'source_sha256': {str(p.relative_to(R)): sha(p) for p in paths},
          'fixture_sha256': sha(out / 'cases.inc'), 'compile_command': command, 'output': run.stdout.strip(),
          'scope': 'Two exact original scalar PCs through real native finite dispatch, generated LLVM cdecl wrappers and native zlib. 200 original x86 outcomes at16 alignments; full CPU/stack/arena. No whole-module activation, original dispatcher ARM execution, guest exception delivery, rendering or online claim.'}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(run.stdout.strip())
