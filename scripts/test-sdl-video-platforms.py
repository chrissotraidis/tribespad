#!/usr/bin/env python3
"""Compile the video bridge and native app entry for Apple mobile targets."""
import hashlib,json,pathlib,subprocess
from sdl_backend_inputs import inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/sdl-video/platforms';out.mkdir(parents=True,exist_ok=True);rows=[]
for platform,target in [('device','arm64-apple-ios16'),('simulator','arm64-apple-ios16-simulator')]:
 backend,flags,_=inputs(platform)
 for name in ['port/core/guest_sdl_backend.c','port/apple/sdl_main.c']:
  source=R/name;obj=out/(platform+'-'+source.stem+'.o');cmd=['clang','-target',target,'-isysroot',backend['sdk'],'-DPTROFS_64BIT','-O2','-Wall','-Wextra','-Werror',*flags,'-c',str(source),'-o',str(obj)];subprocess.run(cmd,check=True)
  rows.append(dict(platform=platform,target=target,source=name,source_sha256=sha(source),object=str(obj.relative_to(R)),object_sha256=sha(obj),backend_sha256=sha(R/'generated/sdl-backend'/platform/'result.json')))
(out/'result.json').write_text(json.dumps(dict(passed=True,rows=rows,script_sha256=sha(pathlib.Path(__file__)),execution_tested=False,scope='Mobile compilation only. UIKit execution is separately tested in the sole simulator app; physical device execution remains unverified.'),indent=2)+'\n');print('PASS: production SDL video bridge and native Apple app entry compile for device and simulator')
