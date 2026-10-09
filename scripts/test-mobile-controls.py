#!/usr/bin/env python3
"""Check touch-to-SDL event semantics and compile UIKit for both mobile targets.

Does not launch another Simulator app or interrupt an active engine run.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
from sdl_backend_inputs import inputs

R=Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/mobile-controls';out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,DEVELOPER_DIR='/Applications/Xcode.app/Contents/Developer')
env['SDKROOT']=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],env=env,text=True).strip()
sources=['tests/mobile_controls_test.c','port/apple/mobile_controls.c','port/core/input.c']
_,flags,libs=inputs()
subprocess.run(['clang','-Wall','-Wextra','-Werror',*flags,*sources,*libs,'-o',str(out/'test')],cwd=R,env=env,check=True)
result=subprocess.run([str(out/'test')],env=env,capture_output=True,text=True,check=True)
print(result.stdout,end='')
for sdk,target in [('iphonesimulator','arm64-apple-ios16-simulator'),('iphoneos','arm64-apple-ios16')]:
    path=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],env=env,text=True).strip()
    subprocess.run(['clang','-target',target,'-isysroot',path,
        '-Igenerated/sdl-backend/simulator/install/include','-fobjc-arc','-Wall','-Wextra','-Werror',
        '-c','port/apple/mobile_controls.m','-o',str(out/(sdk+'.o'))],cwd=R,env=env,check=True)
sources+=['port/apple/mobile_controls.h','port/apple/mobile_controls.m','port/core/input.h',
          'scripts/test-mobile-controls.py']
(out/'result.json').write_text(json.dumps(dict(passed=True,output=result.stdout,
    sources_sha256={s:hashlib.sha256((R/s).read_bytes()).hexdigest() for s in sources},
    scope='Native SDL event delivery and UIKit compilation; not original mobile gameplay acceptance.'),indent=2)+'\n')
