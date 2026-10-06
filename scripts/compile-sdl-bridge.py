#!/usr/bin/env python3
"""Compile the SDL guest bridge for Apple mobile targets; do not run a simulator."""
import hashlib,json,pathlib,subprocess
from sdl_backend_inputs import inputs
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
reports={}
for platform,sdk,target in [('device','iphoneos','arm64-apple-ios16'),('simulator','iphonesimulator','arm64-apple-ios16-simulator')]:
    build,flags,_=inputs(platform);root=R/'generated/sdl-backend'/platform
    source=R/'port/core/guest_sdl_backend.c';obj=root/'guest_sdl_backend.o'
    command=['xcrun','--sdk',sdk,'clang','-target',target,'-isysroot',build['sdk'],'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror',*flags,'-c',str(source),'-o',str(obj)]
    subprocess.run(command,check=True)
    reports[platform]=dict(command=command,object_sha256=sha(obj),source_sha256=sha(source),backend_sha256=sha(root/'result.json'),execution_tested=False)
out=R/'generated/integration-review/sdl-hints/mobile-compilation.json'
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(dict(passed=True,platforms=reports,script_sha256=sha(pathlib.Path(__file__))),indent=2)+'\n')
print('PASS: SDL guest bridge compiles for device and Simulator; not executed')
