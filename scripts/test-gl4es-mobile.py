#!/usr/bin/env python3
"""Render legacy desktop GL through GL4ES in one already-booted iPad Simulator."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--simulator-device', required=True)
args = parser.parse_args()
env = dict(os.environ, DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')


def run(command, **kwargs):
    return subprocess.run(command, env=env, check=True, **kwargs)


devices = json.loads(run(['xcrun', 'simctl', 'list', 'devices', 'booted', '--json'], capture_output=True, text=True).stdout)
booted = [d for group in devices['devices'].values() for d in group if d['state'] == 'Booted']
assert len(booted) == 1 and booted[0]['udid'] == args.simulator_device
assert 'iPad' in booted[0]['deviceTypeIdentifier']
library = ROOT / 'generated/gl4es/simulator/lib/libGL.a'
build = json.loads((library.parents[1] / 'result.json').read_text())
assert hashlib.sha256(library.read_bytes()).hexdigest() == build['library_sha256']
out = ROOT / 'generated/integration-review/gl4es-mobile'
app = out / 'TribesRenderer.app'
app.mkdir(parents=True, exist_ok=True)
identifier = 'org.tribespad.gl4es-probe'
subprocess.run(['xcrun', 'simctl', 'terminate', args.simulator_device, identifier], env=env, capture_output=True)
info = dict(CFBundleIdentifier=identifier, CFBundleExecutable='TribesRenderer',
            CFBundleName='TribesRenderer', CFBundlePackageType='APPL', CFBundleVersion='1',
            CFBundleShortVersionString='1.0', MinimumOSVersion='16.0', UIDeviceFamily=[1, 2], UILaunchScreen={})
(app / 'Info.plist').write_bytes(plistlib.dumps(info))
shutil.copy2(library.parents[1] / 'LICENSE.gl4es', app / 'LICENSE.gl4es')
sdk = run(['xcrun', '--sdk', 'iphonesimulator', '--show-sdk-path'], capture_output=True, text=True).stdout.strip()
source = ROOT / 'tests/gl4es_mobile_probe.m'
run(['xcrun', '--sdk', 'iphonesimulator', 'clang', '-target', 'arm64-apple-ios16-simulator',
     '-isysroot', sdk, '-fobjc-arc', '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
     '-I' + str(ROOT / 'ref/gl4es/include'), str(source), str(library),
     '-framework', 'UIKit', '-framework', 'QuartzCore', '-framework', 'OpenGLES', '-o', str(app / 'TribesRenderer')])
run(['codesign', '--force', '--sign', '-', str(app)], capture_output=True)
run(['xcrun', 'simctl', 'install', args.simulator_device, str(app)])
container = Path(run(['xcrun', 'simctl', 'get_app_container', args.simulator_device, identifier, 'data'], capture_output=True, text=True).stdout.strip())
result_path = container / 'Documents/result.json'
if result_path.exists():
    result_path.unlink()
for name in ('stdout.log', 'stderr.log'):
    (out / name).write_text('')
run(['xcrun', 'simctl', 'launch', '--stdout=' + str(out / 'stdout.log'), '--stderr=' + str(out / 'stderr.log'), args.simulator_device, identifier])
deadline = time.monotonic() + 45
while not result_path.exists() and time.monotonic() < deadline:
    time.sleep(.25)
if not result_path.exists():
    raise RuntimeError('Renderer did not write a receipt; inspect stdout/stderr and app crash logs')
result = json.loads(result_path.read_text())
result.update(dependency=build, device=booted[0]['name'], simulator_udid=args.simulator_device,
              source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
              executable_sha256=hashlib.sha256((app / 'TribesRenderer').read_bytes()).hexdigest())
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
assert result['passed'] and result['initialization_error'] == 0, result
run(['xcrun', 'simctl', 'io', args.simulator_device, 'screenshot', str(out / 'screen.png')])
print('PASS: native iPad Simulator textured immediate/indexed rendering; original engine not executed')
