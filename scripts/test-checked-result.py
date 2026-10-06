#!/usr/bin/env python3
"""Exercise the bounded x87 scratch allocator with both runtime checks enabled."""
import hashlib,json,pathlib,signal,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/checked-result';out.mkdir(exist_ok=True)
sources=[R/'port/llasm-support/tribes_result.c',R/'port/core/guest_heap.c']
objects,build_sha=compile_support(sources,out/'checked',['-DPTROFS_64BIT'])
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for name in objects:
 path=pathlib.Path(name);row=meta['objects'][path.stem];entries.append(dict(object=name,object_sha256=sha(path),bitcode=str(path.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guard_sha=guard_link(entries,out/'guarded')
trusted=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c',R/'tests/tribes_result_test.c']
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT','-DTB_CHECKED_RESULT',*objects,*map(str,trusted),'-o',str(out/'test')],check=True)
run=subprocess.run([str(out/'test')],capture_output=True,text=True,timeout=30)
(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr)
assert run.returncode==0 and run.stderr.count('checked guest memory violation')==2,(run.returncode,run.stdout,run.stderr)
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin')
# Identical instrumented code with an unreviewed source identity must still stop
# before mprotect. This detects accidentally admitting it as a generic call.
unreviewed=out/'unreviewed.ll'
subprocess.run([str(tools/'llvm-dis'),entries[0]['bitcode'],'-o',str(unreviewed)],check=True)
ir=unreviewed.read_text();needle='source_filename = "'+str(sources[0])+'"';assert ir.count(needle)==1
unreviewed.write_text(ir.replace(needle,'source_filename = "unreviewed-allocator.c"'))
subprocess.run([str(tools/'clang'),'-target','arm64-apple-macos12','-O0','-c',str(unreviewed),'-o',str(out/'unreviewed.o')],check=True)
negative_entries=[dict(object=str(out/'unreviewed.o'),object_sha256=sha(out/'unreviewed.o'),bitcode=str(unreviewed),bitcode_sha256=sha(unreviewed)),entries[1]]
negative_objects,negative_sha=guard_link(negative_entries,out/'unreviewed-guarded')
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT','-DTB_CHECKED_RESULT','-DTB_EXPECT_MANAGER_STOP',*negative_objects,*map(str,trusted),'-o',str(out/'unreviewed-test')],check=True)
negative=subprocess.run([str(out/'unreviewed-test')],capture_output=True,text=True,timeout=30)
(out/'unreviewed.log').write_text(negative.stdout+negative.stderr)
assert negative.returncode==-signal.SIGABRT and 'unverified call: mprotect' in negative.stderr,(negative.returncode,negative.stderr)
loader_sources=[R/'port/core'/(n+'.c') for n in ['guest_address_space','guest_virtual_memory','process_heap']]
loader_objects,loader_sha=compile_support(loader_sources,out/'loader',['-DPTROFS_64BIT'])
loader_meta=json.loads((out/'loader/result.json').read_text());loader_entries=list(entries)
for name in loader_objects:
 path=pathlib.Path(name);row=loader_meta['objects'][path.stem];loader_entries.append(dict(object=name,object_sha256=sha(path),bitcode=str(path.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
loader_objects,loader_guard_sha=guard_link(loader_entries,out/'loader-guarded')
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT','-DTB_CHECKED_RESULT','-DTB_CHECKED_RESULT_LOADER',*loader_objects,*map(str,trusted),'-o',str(out/'loader-test')],check=True)
loader_run=subprocess.run([str(out/'loader-test')],capture_output=True,text=True,timeout=30)
(out/'loader.log').write_text(loader_run.stdout+loader_run.stderr)
assert loader_run.returncode==0 and loader_run.stderr.count('checked guest memory violation')==2,(loader_run.returncode,loader_run.stdout,loader_run.stderr)
report=dict(passed=True,checked_build_sha256=build_sha,guard_build_sha256=guard_sha,negative_guard_sha256=negative_sha,loader_build_sha256=loader_sha,loader_guard_sha256=loader_guard_sha,binary_sha256=sha(out/'test'),loader_binary_sha256=sha(out/'loader-test'),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+loader_sources+trusted+[pathlib.Path(__file__).resolve(),R/'port/core/guest_address_space.h']},scope='16000 concurrent wide results both standalone and with sealed checked loader map. Scratch commits agree with VirtualQuery and checked permissions, preserve neighboring pages and later read-only grants; checked source/destination faults preserve shared bytes. Unreviewed standalone allocator stops before mprotect.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
print(loader_run.stdout,end='')
