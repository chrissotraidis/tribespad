#!/usr/bin/env python3
"""Execute the finite DirectSound device ABI against actual CoreAudio devices."""
import hashlib,importlib.util,json,pathlib,subprocess
from dsound_backend_inputs import prepare
from sdl_backend_inputs import inputs
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/dsound-device';out.mkdir(parents=True,exist_ok=True)
sources,bindings=prepare(out);_,flags,libs=inputs()
spec=importlib.util.spec_from_file_location('gen',R/'scripts/gen-win32-bindings.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
callback=out/'callback.ll';callback.write_text(g.generate({'TestSoundCallback':dict(helper='test_sound_callback',arguments=4,returns=True,guest_base=False,cpu_argument=True)},False))
sources += [callback,R/'tests/dsound_device_test.c',R/'tests/fixtures/native_enter.ll',R/'port/core/guest_memory_checks.c',R/'port/llasm-support/tribes_absolute_memory.c']
flags+=['-DTB_DSOUND_TEST','-DPTROFS_64BIT','-I',str(out),'-I',str(R/'port/core'),'-Wno-override-module']
runs=[]
for mode in ['normal','checked','tsan']:
 build=R/'generated/native-win32'/('checked-macos' if mode=='checked' else 'macos');meta=json.loads((build/'result.json').read_text());assert mode!='checked' or meta.get('state')=='finished';objects=[]
 for name,row in meta['objects'].items():
  if name=='guest_dispatch':continue
  obj=build/(name+'.o');assert sha(obj)==row['object_sha256'];objects.append(str(obj))
 if mode=='checked':new,_=compile_support(sources,out/'checked',flags)
 else:new=list(map(str,sources))
 exe=out/("native-"+mode);cmd=['clang','-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),'-O2','-target','arm64-apple-macos12']+(['-fsanitize=thread'] if mode=='tsan' else [])+flags+new+objects+[str(R/'generated/softfloat/macos/softfloat.a')]+libs+['-framework','Foundation','-framework','AppKit','-o',str(exe)]
 c=subprocess.run(cmd,capture_output=True,text=True);(out/(mode+'-build.log')).write_text(c.stdout+c.stderr);assert c.returncode==0,c.stderr
 r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'-run.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
 faults=[]
 if mode=='checked':
  for case in ['readonly','unreadable-guid','readonly-format','readonly-lock','unreadable-pcm','readonly-listener']:
   r=subprocess.run([str(exe),case],capture_output=True,text=True,timeout=30);assert r.returncode==-6 and 'checked guest memory violation' in r.stderr,(case,r.returncode,r.stderr);faults.append(case)
 runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),faults=faults));print(mode,'PASS',flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[R/'port/core/guest_dsound.h',pathlib.Path(__file__).resolve()]},scope='Real native output device open/query/pause/close and enumeration through original-width generated ABI, callback reentrancy and CPU preservation; checked guest input/output faults. Window identity is a test fixture; real-window acceptance requires full engine. Primary format and secondary checked ring writes, real CoreAudio postmix samples, volume/pan, cursor advancement, stop, looping wrap and finite completion. Primary 3D listener COM identity/lifetime, all method ABI, requested versus applied/deferred parameters, validation and protected output. Secondary positional audio is not accepted. No original engine or mobile acceptance.'),indent=2)+'\n')
