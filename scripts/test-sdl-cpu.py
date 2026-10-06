#!/usr/bin/env python3
"""Check SDL CPU query bindings on macOS or the sole booted Simulator."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator');a=ap.parse_args()
if a.simulator:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator else ('macosx','arm64-apple-macos12')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/sdl-cpu'/('simulator' if a.simulator else 'macos');out.mkdir(parents=True,exist_ok=True)
reference=R/'config/runtime/sdl-cpu-reference.json';refs=json.loads(reference.read_text())['references']
for row in refs:assert sha(R/row['path'])==row['sha256']
profile=R/'config/runtime/cpu.json';assert json.loads(profile.read_text())['features']==['x87','MMX']
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';data=json.loads(bindings.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:data[n] for n in ['SDL_HasMMX','SDL_HasSSE','SDL_HasAVX']},False))
sources=[R/'port/core/guest_cpu.c',R/'port/core/guest_processor_features.c',R/'tests/fixtures/sdl_cpu_dispatch.c',R/'tests/fixtures/native_enter.ll',wrapper]
objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT','-DCHECKED_TEST'],target=target,sdk_name=sdk_name)
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded,target=target,simulator=a.simulator)
for mode in (['checked'] if a.simulator else ['checked','normal','tsan']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,guards),str(R/'tests/sdl_cpu_test.c')]
 cmd+=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator,str(exe)] if a.simulator else [str(exe)],capture_output=True,text=True,timeout=45)
 (out/(mode+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 report[mode]=dict(passed=True,binary_sha256=sha(exe),stdout=run.stdout);print(mode,run.stdout.strip())
sources += guards+[R/'tests/sdl_cpu_test.c',profile,reference,bindings,R/'port/core/guest_cpu.h',R/'port/core/guest_processor_features.h',R/'port/core/guest_system.h',R/'scripts/gen-win32-bindings.py',pathlib.Path(__file__).resolve()]+[R/row['path'] for row in refs]
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources},scope='Three imported SDL CPU feature queries against the fixed x86 guest profile. No ARM host feature advertisement, AVX context contract, SDL hint overrides or full SDL integration.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
