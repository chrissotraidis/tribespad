#!/usr/bin/env python3
"""Compile staged finite native routes; does not link or activate the full DLL."""
import argparse, hashlib, importlib.util, json, pathlib, subprocess
R = pathlib.Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(); ap.add_argument('--platform', choices=['macos', 'device', 'simulator'], default='macos'); a = ap.parse_args()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
out = R / 'generated/zlib-ng' / a.platform / 'compiled-routes'; out.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('routes', R / 'scripts/prepare-qol-zlib-routes.py')
routes = importlib.util.module_from_spec(spec); spec.loader.exec_module(routes)
prepared = routes.prepare(out / 'prepared')
sdkname, target = {'macos': ('macosx', 'arm64-apple-macos12'),
                  'device': ('iphoneos', 'arm64-apple-ios16'),
                  'simulator': ('iphonesimulator', 'arm64-apple-ios16-simulator')}[a.platform]
sdk = subprocess.check_output(['xcrun', '--sdk', sdkname, '--show-sdk-path'], text=True).strip()
sources = [out / 'prepared/bindings.ll', out / 'prepared/dispatch.ll', R / 'port/core/guest_dispatch.c']
objects = []
for p in sources:
    obj = out / (p.stem + '.o')
    cmd = ['clang', '-target', target, '-isysroot', sdk, '-O2', '-Wall', '-Wextra',
           '-Werror', '-Wno-override-module', '-DPTROFS_64BIT', '-c', str(p), '-o', str(obj)]
    subprocess.run(cmd, check=True)
    objects.append({'source': str(p.relative_to(R)), 'source_sha256': sha(p),
                    'object': str(obj.relative_to(R)), 'object_sha256': sha(obj), 'command': cmd})
report = {'pass': True, 'target': target, 'sdk': sdk, 'routes': prepared, 'objects': objects,
          'cpu_header_sha256': sha(R / 'port/llasm-support/llasm_cpu.h'), 'builder_sha256': sha(pathlib.Path(__file__)),
          'scope': 'Compiled finite native cdecl wrappers and two-entry original-PC dispatch model. Whole-image route merge, direct transfer replacement and full DLL/client linking remain incomplete.'}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: staged native scalar route objects ' + a.platform)
