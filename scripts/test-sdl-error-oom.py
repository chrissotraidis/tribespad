#!/usr/bin/env python3
"""Check the native SDL emergency error patch with real failing allocators."""
import argparse,hashlib,json,pathlib,subprocess
from sdl_backend_inputs import inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for row in d['devices'].values() for x in row if x['state']=='Booted']==[a.simulator_device]
platform='simulator' if a.simulator_device else 'macos';backend,flags,libraries=inputs(platform);out=R/'generated/integration-review/sdl-video/error-oom'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"passed":false}\n');test=R/'tests/sdl_error_oom_test.c';runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target','arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12','-isysroot',backend['sdk'],'-O1','-g','-Wall','-Wextra','-Werror',*flags,str(test)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+libraries+['-o',str(exe)],check=True)
 x=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'.log')).write_text(x.stdout+x.stderr);assert not x.returncode and x.stdout.startswith('PASS:') and 'ThreadSanitizer:' not in x.stderr,(x.returncode,x.stdout,x.stderr);runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),stdout=x.stdout));print(mode,x.stdout.strip())
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,simulator_device=a.simulator_device,backend_sha256=sha(R/'generated/sdl-backend'/platform/'result.json'),source_sha256={str(p.relative_to(R)):sha(p) for p in [test,R/'config/runtime/sdl-backend.json',pathlib.Path(__file__).resolve()]},scope='Native SDL persistent OOM/error recursion, fallback thread isolation and recovery. No video driver/window or guest execution claim.'),indent=2)+'\n')
