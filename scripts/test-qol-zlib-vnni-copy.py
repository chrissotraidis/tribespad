#!/usr/bin/env python3
"""Compare native VNNI Adler copy entry with the explicit offline vector model."""
import argparse, hashlib, importlib.util, json, pathlib, subprocess
R = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(); ap.add_argument('--simulator-device'); a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
platform = 'simulator' if a.simulator_device else 'macos'
out = R / 'generated/zlib-ng' / platform / ('vnni-copy-' + (a.simulator_device or 'macos'))
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
reference_path = R / 'generated/qol-vnni-copy-reference/result.json'
reference = json.loads(reference_path.read_text())
assert reference['pass']
assert reference['image_sha256'] == sha(R / 'ref/product/IFC22.dll')
for name, h in reference['source_sha256'].items(): assert sha(R / name) == h
cases = [dict(c, copy=1) for c in reference['cases']]
assert len(cases) == 1465
(out / 'cases.inc').write_text(
    'struct checksum_case {uint32_t length,seed,copy,result,source_null,destination_null,pattern;};\n'
    'static const struct checksum_case cases[]={\n' + ''.join(
        '{%du,%du,%du,%du,%du,%du,%du},\n' % tuple(v[k] for k in
            ['length', 'seed', 'copy', 'result', 'source_null', 'destination_null', 'pattern'])
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
           R / 'tests/qol_zlib_vnni_copy_test.c', out / 'routes/bindings.ll', out / 'routes/dispatch.ll']
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
          'cases': 24815, 'protected_page_cases': 1375, 'original_reference_sha256': sha(reference_path),
          'routes': prepared, 'library_sha256': sha(lib), 'binary_sha256': sha(exe),
          'source_sha256': {str(p.relative_to(R)): sha(p) for p in paths},
          'fixture_sha256': sha(out / 'cases.inc'), 'compile_command': command, 'output': run.stdout.strip(),
          'scope': 'Original VNNI checksum-copy PC through real native finite dispatch, generated cdecl wrapper and native zlib. 1465 explicit offline vector-model outcomes without substituted callees, at16 alignments plus 1375 inaccessible-page boundary checks. Native ABI CPU/stack/buffers preserve their declared cdecl contract; original private argument scratch/vector clobbers are not reproduced. No hardware AVX512 proof, whole-client execution, rendering or online claim.'}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(run.stdout.strip())
