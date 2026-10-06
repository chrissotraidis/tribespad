#!/usr/bin/env python3
"""Link all reached curl imports for an Apple target; does not run a simulator."""
import argparse
import hashlib
import json
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
parser = argparse.ArgumentParser()
parser.add_argument('--platform', choices=['macos', 'device', 'simulator'], required=True)
args = parser.parse_args()
build = ROOT / 'generated/http-backend' / args.platform
report = json.loads((build / 'result.json').read_text())
assert report['passed'] and report['platform'] == args.platform
assert report['config_sha256'] == sha(ROOT / 'config/runtime/http-backend.json')
for name, expected in report['libraries'].items():
    assert sha(ROOT / name) == expected, name
boundary = ROOT / 'generated/integration-review/network-publication-checkpoint-20261002/curl-native-boundary.json'
imports = [row['name'] for row in json.loads(boundary.read_text())['imports']]
assert len(imports) == 23
out = ROOT / 'generated/integration-review/http-backend-link' / args.platform
out.mkdir(parents=True, exist_ok=True)
result = out / 'result.json'
if result.exists():
    result.unlink()
source = ROOT / 'tests/http_backend_test.c'
target = dict(macos='arm64-apple-macos12', device='arm64-apple-ios16', simulator='arm64-apple-ios16-simulator')[args.platform]
platform = dict(macos='MACOS', device='IOS', simulator='IOSSIMULATOR')[args.platform]
executable = out / 'http-backend-link'
prefix = build / 'install'
command = ['clang', '-target', target, '-isysroot', report['sdk'], '-O1', '-g',
           '-Wall', '-Wextra', '-Werror', '-I', str(prefix / 'include'), str(source),
           *[str(prefix / 'lib' / name) for name in ['libcurl.a', 'libssl.a', 'libcrypto.a']], '-lz']
if args.platform == 'macos':
    command += ['-framework', 'SystemConfiguration', '-framework', 'CoreFoundation', '-framework', 'CoreServices']
command += ['-o', str(executable)]
run = subprocess.run(command, capture_output=True, text=True)
(out / 'link.log').write_text(run.stdout + run.stderr)
assert run.returncode == 0, run.stderr
symbols = subprocess.check_output(['nm', '-gU', str(executable)], text=True)
defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
assert all('_' + name in defined for name in imports)
linked = subprocess.check_output(['otool', '-L', str(executable)], text=True)
assert all(value not in linked for value in ['libcurl', 'libssl', 'libcrypto', '/opt/', '/usr/local/'])
version = subprocess.check_output(['vtool', '-show-build', str(executable)], text=True)
assert 'platform ' + platform + '\n' in version, version
(out / 'linked-libraries.txt').write_text(linked)
(out / 'build-version.txt').write_text(version)
result.write_text(json.dumps(dict(passed=True, platform=args.platform, command=command,
                                 binary_sha256=sha(executable), source_sha256=sha(source),
                                 script_sha256=sha(pathlib.Path(__file__)),
                                 build_report_sha256=sha(build / 'result.json'),
                                 boundary_sha256=sha(boundary), resolved_imports=imports,
                                 scope='Link and Mach-O platform validation only; no mobile execution, guest ABI, original engine or online acceptance.'), indent=2) + '\n')
print('PASS: all 23 native curl imports linked for ' + args.platform + '; not executed')
