#!/usr/bin/env python3
"""Execute original-width OpenAL ABI against real CoreAudio, including guards."""
import hashlib,json,os,pathlib,subprocess,sys
from openal_backend_inputs import prepare,inputs
from checked_support import compile_support
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/openal-provider/abi';out.mkdir(parents=True,exist_ok=True)
sources,bindings=prepare(out);backend,flags,libs=inputs();math=sse_inputs('macos');flags+=math['compile_flags'];libs+=math['libraries']
bochs=R/'generated/bochs-math';bm=json.loads((bochs/'result.json').read_text());assert sha(bochs/'math_bridge.o')==bm['native_bridge_object_sha256'] and sha(bochs/'libbochs-math.a')==bm['library_sha256'];libs += [str(bochs/'math_bridge.o'),str(bochs/'libbochs-math.a')]
sources += [R/p for p in ['tests/openal_guest_test.c','tests/fixtures/native_enter.ll','port/core/guest_memory_checks.c','port/llasm-support/tribes_absolute_memory.c','port/core/ext80.c','port/llasm-support/tribes_float80.c']]
flags+=['-DTB_OPENAL_TEST','-D_DEFAULT_SOURCE','-DPTROFS_64BIT','-I',str(out),'-I',str(R/'port/core'),'-Wno-override-module']
offline='--offline' in sys.argv
env=dict(os.environ,ALSOFT_DRIVERS='core',ALSOFT_LOGLEVEL='2',ALSOFT_CONF='/dev/null');runs=[]
for mode in ['normal','checked']:
 build=R/'generated/native-win32'/('checked-macos' if mode=='checked' else 'macos');meta=json.loads((build/'result.json').read_text());objects=[]
 for name,row in meta['objects'].items():
  if name=='guest_dispatch':continue
  obj=build/(name+'.o');assert sha(obj)==row['object_sha256'];objects.append(str(obj))
 if mode=='checked':new,_=compile_support(sources,out/'checked',flags)
 else:new=list(map(str,sources))
 exe=out/('native-'+mode);cmd=['clang','-isysroot',backend['sdk'],'-O2','-target','arm64-apple-macos12']+flags+new+objects+libs+['-framework','Foundation','-framework','AppKit','-o',str(exe)]
 c=subprocess.run(cmd,capture_output=True,text=True);(out/(mode+'-build.log')).write_text(c.stdout+c.stderr);assert not c.returncode,c.stderr
 r=subprocess.run([str(exe)]+(['normal','offline'] if offline else []),env=env,capture_output=True,text=True,timeout=30);(out/(mode+'-run.log')).write_text(r.stdout+r.stderr);assert not r.returncode,(r.returncode,r.stdout,r.stderr)
 faults=[]
 for case in (['readonly','unreadable-pcm','unreadable-attributes','readonly-efx','unreadable-efx-vector','readonly-efx-vector','stale-device'] if mode=='checked' else ['stale-device'])+(['stale-context'] if offline else []):
  r=subprocess.run([str(exe),case]+(['offline'] if offline else []),env=env,capture_output=True,text=True,timeout=30);assert r.returncode==-6 and ('invalid context token' if case=='stale-context' else 'invalid device token' if case=='stale-device' else 'checked guest memory violation') in r.stderr,(case,r.returncode,r.stderr);faults.append(case)
 runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),faults=faults));print(mode,'PASS',flush=True)
(out/('offline-result.json' if offline else 'result.json')).write_text(json.dumps(dict(passed=True,offline=offline,runs=runs,library_sha256=backend['library_sha256'],sources={str(p.relative_to(R)):sha(p) for p in sources},scope='Actual finite cdecl OpenAL guest bridge; '+('offline positional mixer' if offline else 'CoreAudio playback')+'; not original-game or mobile acceptance.'),indent=2)+'\n')
