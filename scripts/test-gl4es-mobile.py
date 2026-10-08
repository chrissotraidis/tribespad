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
parser.add_argument('--sdl', action='store_true', help='Exercise the SDL context adapter and shared textures')
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
out = ROOT / 'generated/integration-review' / ('gl4es-sdl' if args.sdl else 'gl4es-mobile')
app = out / 'TribesRenderer.app'
app.mkdir(parents=True, exist_ok=True)
identifier = 'org.tribespad.gl4es-sdl' if args.sdl else 'org.tribespad.gl4es-probe'
subprocess.run(['xcrun', 'simctl', 'terminate', args.simulator_device, identifier], env=env, capture_output=True)
info = dict(CFBundleIdentifier=identifier, CFBundleExecutable='TribesRenderer',
            CFBundleName='TribesRenderer', CFBundlePackageType='APPL', CFBundleVersion='1',
            CFBundleShortVersionString='1.0', MinimumOSVersion='16.0', UIDeviceFamily=[1, 2],
            UILaunchScreen={}, UIApplicationSupportsIndirectInputEvents=True)
(app / 'Info.plist').write_bytes(plistlib.dumps(info))
shutil.copy2(library.parents[1] / 'LICENSE.gl4es', app / 'LICENSE.gl4es')
sdk = run(['xcrun', '--sdk', 'iphonesimulator', '--show-sdk-path'], capture_output=True, text=True).stdout.strip()
source = ROOT / ('tests/gl4es_sdl_probe.c' if args.sdl else 'tests/gl4es_mobile_probe.m')
flags = ['-fobjc-arc']
libraries = ['-framework', 'UIKit', '-framework', 'QuartzCore', '-framework', 'OpenGLES']
sources = [source]
if args.sdl:
    from sdl_backend_inputs import inputs
    sdl, flags, libraries = inputs('simulator')
    flags += ['-DNOX11', '-DNOEGL', '-DNO_LOADER', '-DDEFAULT_ES=2',
              '-isystem', str(ROOT / 'ref/gl4es/src')]
    sources += [ROOT / 'port/apple/gl4es_context.c']
run(['xcrun', '--sdk', 'iphonesimulator', 'clang', '-target', 'arm64-apple-ios16-simulator',
     '-isysroot', sdk, *flags, '-Wall', '-Wextra', '-Werror', '-Wno-deprecated-declarations',
     '-I' + str(ROOT / 'ref/gl4es/include'), *map(str,sources), str(library),
     *libraries, '-o', str(app / 'TribesRenderer')])
run(['codesign', '--force', '--sign', '-', str(app)], capture_output=True)
run(['xcrun', 'simctl', 'install', args.simulator_device, str(app)])
container = Path(run(['xcrun', 'simctl', 'get_app_container', args.simulator_device, identifier, 'data'], capture_output=True, text=True).stdout.strip())
result_path = container / 'Documents/result.json'
if result_path.exists():
    result_path.unlink()
rendered_path = container / 'Documents/rendered.json'
if rendered_path.exists():
    rendered_path.unlink()
for name in ('stdout.log', 'stderr.log'):
    (out / name).write_text('')
run(['xcrun', 'simctl', 'launch', '--stdout=' + str(out / 'stdout.log'), '--stderr=' + str(out / 'stderr.log'), args.simulator_device, identifier])
deadline = time.monotonic() + 45
captured = False
while not result_path.exists() and time.monotonic() < deadline:
    if args.sdl and rendered_path.exists() and not captured:
        run(['xcrun', 'simctl', 'io', args.simulator_device, 'screenshot', str(out / 'screen.png')])
        captured = True
    time.sleep(.25)
if not result_path.exists():
    raise RuntimeError('Renderer did not write a receipt; inspect stdout/stderr and app crash logs')
result = json.loads(result_path.read_text())
result.update(dependency=build, device=booted[0]['name'], simulator_udid=args.simulator_device,
              source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
              executable_sha256=hashlib.sha256((app / 'TribesRenderer').read_bytes()).hexdigest())
if args.sdl:
    result['sdl_backend_sha256'] = hashlib.sha256((ROOT / 'generated/sdl-backend/simulator/result.json').read_bytes()).hexdigest()
    result['adapter_sha256'] = hashlib.sha256((ROOT / 'port/apple/gl4es_context.c').read_bytes()).hexdigest()
(out / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
assert result['passed'], result
if args.sdl:
    assert result['cleanup_passed'] and result['borrowed_framebuffer_preserved'] and result['pixel_checks']==6
else:
    assert result['initialization_error']==0
    run(['xcrun', 'simctl', 'io', args.simulator_device, 'screenshot', str(out / 'screen.png')])
print('PASS: native iPad Simulator renderer' + (' with SDL context sharing and cleanup' if args.sdl else '') + '; original engine not executed')
