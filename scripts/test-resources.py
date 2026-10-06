#!/usr/bin/env python3
"""Exercise original resource data through emitted Win32 ABIs and checked runtime."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess,zipfile
import pefile
from checked_support import compile_support
from guard_checked_link import guard_link
from image_resources import image_resources,resource_header
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 devices=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [v['udid'] for group in devices['devices'].values() for v in group if v['state']=='Booted']==[a.simulator_device]
target,sdk_name=('arm64-apple-ios16-simulator','iphonesimulator') if a.simulator_device else ('arm64-apple-macos12','macosx')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
out=R/'generated/integration-review/resources';out=out/a.simulator_device if a.simulator_device else out;out.mkdir(parents=True,exist_ok=True)
image=pefile.PE(str(R/'ref/product/IFC22.dll'));rows=image_resources(image);mapped=image.get_memory_mapped_image();(out/'image.bin').write_bytes(mapped)
assert len(rows)==3 and rows[0]['sha256']=='d59c646b87f18742132a6eb326ad3280476e58ab4de4f3ac3c55af9cda26b27e'
(out/'resources.inc').write_text('#define IMAGE_SIZE %du\n#define MAPPED_SIZE %du\n'%(image.OPTIONAL_HEADER.SizeOfImage,len(mapped))+resource_header('resources',rows))
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';data=json.loads(bindings.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({n:data[n] for n in ['FindResourceA','SizeofResource','LoadResource','LockResource']},False))
sources=[R/'port/core/guest_modules.c',R/'port/core/guest_heap.c',R/'tests/fixtures/resource_dispatch.c',R/'tests/fixtures/native_enter.ll',wrapper]
objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT'],target=target,sdk_name=sdk_name);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target);guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/resource_test.c';runs=[]
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror','-I',str(out),*map(str,guards),str(test)]
 cmd+=(['-DCHECKED_TEST']+objects) if mode=='checked' else list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True)
 extracted=out/(mode+'-resource.zip');run=subprocess.run((['xcrun','simctl','spawn',a.simulator_device] if a.simulator_device else [])+[str(exe),str(out/'image.bin'),str(extracted)],capture_output=True,text=True,timeout=60);(out/(mode+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 assert sha(extracted)==rows[0]['sha256'];z=zipfile.ZipFile(extracted);assert len(z.infolist())==14 and z.testzip() is None
 runs.append(dict(mode=mode,binary_sha256=sha(exe),resource_sha256=sha(extracted),files=z.namelist(),passed=True));print(mode,run.stdout.strip(),flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=True,simulator_device=a.simulator_device,runs=runs,resources=rows,checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+guards+[test,bindings,R/'scripts/image_resources.py',R/'port/core/guest_modules.h']}),indent=2)+'\n')
