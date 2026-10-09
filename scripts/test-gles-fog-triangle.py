#!/usr/bin/env python3
"""Reproduce the captured interior triangle directly in native Simulator GLES."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import time

R=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--simulator-device',required=True)
args=parser.parse_args()
env=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
def run(command,**kwargs):
    return subprocess.run(command,env=env,check=True,**kwargs)
devices=json.loads(run(['xcrun','simctl','list','devices','booted','--json'],capture_output=True,text=True).stdout)
booted=[d for group in devices['devices'].values() for d in group if d['state']=='Booted']
assert len(booted)==1 and booted[0]['udid']==args.simulator_device
out=R/'generated/integration-review/gles-fog-triangle';app=out/'FogTriangle.app';app.mkdir(parents=True,exist_ok=True)
identifier='org.tribespad.native-fog-triangle'
source=R/'tests/gles_fog_triangle_probe.m';fixture=R/'tests/fixtures/gles-fog-triangle.json'
shutil.copy2(fixture,app/'fixture.json')
(app/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier=identifier,CFBundleExecutable='FogTriangle',CFBundleName='FogTriangle',CFBundlePackageType='APPL',CFBundleVersion='1',CFBundleShortVersionString='1.0',MinimumOSVersion='16.0',UIDeviceFamily=[2],UILaunchScreen={})))
sdk=run(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],capture_output=True,text=True).stdout.strip()
run(['xcrun','clang','-target','arm64-apple-ios16-simulator','-isysroot',sdk,'-fobjc-arc','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations',str(source),'-framework','UIKit','-framework','OpenGLES','-o',str(app/'FogTriangle')])
run(['codesign','--force','--sign','-',str(app)],capture_output=True)
run(['xcrun','simctl','install',args.simulator_device,str(app)])
container=Path(run(['xcrun','simctl','get_app_container',args.simulator_device,identifier,'data'],capture_output=True,text=True).stdout.strip())
receipt=container/'Documents/result.json'
if receipt.exists():receipt.unlink()
try:
    run(['xcrun','simctl','launch','--stdout='+str(out/'stdout.log'),'--stderr='+str(out/'stderr.log'),args.simulator_device,identifier])
    deadline=time.monotonic()+45
    while not receipt.exists() and time.monotonic()<deadline:time.sleep(.25)
    if not receipt.exists():raise RuntimeError('Probe did not write a result; inspect its logs')
    result=json.loads(receipt.read_text())
    result.update(simulator_udid=args.simulator_device,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),scope='Native GLES component only. No original engine or GL4ES executes in this probe.')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    assert result['passed'],'Fog changes rasterized coverage in the captured triangle'
finally:
    subprocess.run(['xcrun','simctl','terminate',args.simulator_device,identifier],env=env,capture_output=True)
