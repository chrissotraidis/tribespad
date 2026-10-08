#!/usr/bin/env python3
"""Execute actual generated cdecl callback wrappers on macOS or one booted simulator."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));booted=[d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted'];assert booted==[a.simulator_device]
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();out=R/'generated/native-win32/cdecl-callback'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator);bindings={name:{'helper':helper,'arguments':count,'guest_base':False,'cpu_argument':True,'returns':returns,'helper_returns':'i32' if returns else 'void','calling_convention':'cdecl'} for name,helper,count,returns in [('FixtureMalloc','fixture_malloc',1,True),('FixtureCalloc','fixture_calloc',2,True),('FixtureRealloc','fixture_realloc',2,True),('FixtureFree','fixture_free',1,False)]};bindings.update({k:v for k,v in json.loads((R/'port/llasm-runtime/win32-bindings.json').read_text()).items() if k in ['SDL_SetMemoryFunctions','SDL_malloc','SDL_calloc','SDL_realloc','SDL_free','SDL_aligned_alloc','SDL_aligned_free']});wrapper=out/'bindings.ll';wrapper.write_text(generator.generate(bindings))
sources=[R/'port/core/guest_sdl_memory.c',R/'port/core/guest_sdl_allocator.c',R/'port/core/guest_callback.c',R/'port/core/guest_heap.c',R/'tests/cdecl_callback_test.c',R/'tests/fixtures/callback.ll',wrapper];runs=[]
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
if not a.simulator_device:
 checked_sources=[p for p in sources if p.name!='cdecl_callback_test.c']
 objects,_=compile_support(checked_sources,out/'checked',['-DPTROFS_64BIT'])
 meta=json.loads((out/'checked/result.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 entries=[dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']) for p in map(pathlib.Path,objects)]
 guarded,_=guard_link(entries,out/'guarded')
for mode in (['normal'] if a.simulator_device else ['normal','checked','tsan']):
 exe=out/(mode+'-test');cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module']
 if mode=='tsan':cmd+=['-fsanitize=thread']
 selected=[R/'tests/cdecl_callback_test.c',*guarded] if mode=='checked' else sources
 compile_result=subprocess.run(cmd+list(map(str,[*selected,*guards]))+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(compile_result.stdout+compile_result.stderr);assert not compile_result.returncode,compile_result.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
faults={}
for name in ['cdecl-as-stdcall','stdcall-as-cdecl','below-stack']+([] if a.simulator_device else ['aligned-free-unreadable']):
 exe=out/('checked-test' if name=='aligned-free-unreadable' else 'normal-test');run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe),name] if a.simulator_device else [str(exe),name],capture_output=True,text=True,timeout=10);(out/(name+'-fault.log')).write_text(run.stdout+run.stderr);assert run.returncode==(134 if a.simulator_device else -6),(name,run.returncode,run.stderr);faults[name]={'returncode':run.returncode,'state':'explicit abort'}
report={'faults':faults,'fixture_bindings':bindings,'generator_sha256':hashlib.sha256((R/'scripts/gen-win32-bindings.py').read_bytes()).hexdigest(),'state' :'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'runs':runs,'sources_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'headers_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [R/'port/llasm-support/llasm_cpu.h',R/'port/core/guest_callback.h',R/'port/core/guest_heap.h',R/'port/core/guest_sdl_memory.h',R/'port/core/guest_sdl_allocator.h']},'bindings_sha256':hashlib.sha256((R/'port/llasm-runtime/win32-bindings.json').read_bytes()).hexdigest(),'scope':'Actual ARM64 cdecl allocator fixtures plus nested stdcall callback dispatch; no selected QoL allocator/SDL module/game-session claim'};report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
