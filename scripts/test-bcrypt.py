#!/usr/bin/env python3
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/bcrypt';out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({'BCryptGenRandom':json.loads(bindings.read_text())['BCryptGenRandom']},False))
sources=[R/n for n in ['port/core/guest_bcrypt.c','port/core/guest_heap.c','tests/fixtures/bcrypt_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper];extra=[R/'tests/fixtures'/n for n in ['bcrypt_external.ll','bcrypt_wrong_width.ll','bcrypt_wrong_return.ll']]
objects,checked=compile_support(sources+extra,out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded');test=R/'tests/bcrypt_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];report=dict(passed=False,checked_sha256=checked,guarded_sha256=guarded)
for mode in ['checked','normal','tsan']:
 exe=out/mode;cmd=['clang','-target','arm64-apple-macos12','-O1','-g','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',str(test),*map(str,guards)]
 if mode=='checked':exe=out/'test-checked';cmd+=['-DCHECKED_TEST']+objects
 else:cmd+=list(map(str,sources))
 if mode=='tsan':cmd+=['-fsanitize=thread']
 subprocess.run(cmd+['-o',str(exe)],check=True);run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'-stdout.log')).write_text(run.stdout);(out/(mode+'-stderr.log')).write_text(run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
 if mode=='checked':assert run.stderr.count('external memory contract stop')==3 and run.stderr.count('unverified call: arc4random_buf')==2,run.stderr
 if mode=='tsan':assert not run.stderr
 report[mode]=dict(passed=True,binary_sha256=sha(exe));print(mode,run.stdout.strip())
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+extra+[test,*guards,R/'port/core/guest_bcrypt.h',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]},scope='Real Apple cryptographic backend, supported system-preferred RNG subset, full emitted guest ABI, concurrent independent buffers, whole-output memory guard and exact host ABI gates. Output difference checks are smoke tests, not a statistical or cryptographic certification. No provider/pseudo-handle or account-authentication acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
