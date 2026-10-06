#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/patch-capture-memory';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources=[R/'port/core'/(n+'.c') for n in ['guest_address_space','guest_virtual_memory','guest_heap','process_heap','guest_memory_checks']]+[R/'tests/patch_capture_memory_test.c']
fixture=R/'generated/integration-review/guest-memory-pass/checked.o';assert sha(fixture)==json.loads((fixture.parent/'result.json').read_text())['checked_object_sha256']
exe=out/'test';subprocess.run(['clang','-target','arm64-apple-macos12','-std=c11','-O2','-pthread','-Wall','-Wextra','-Werror']+list(map(str,sources))+[str(fixture),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'run.log').write_text(run.stdout+run.stderr)
assert run.returncode==0 and run.stdout.startswith('PASS:') and run.stderr.count('checked guest memory violation')==2,(run.returncode,run.stdout,run.stderr)
(out/'result.json').write_text(json.dumps(dict(passed=True,output=run.stdout.strip(),binary_sha256=sha(exe),checked_store_sha256=sha(fixture),source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[pathlib.Path(__file__).resolve(),R/'port/core/guest_address_space.h',R/'port/core/guest_virtual_memory.h']},scope='Single-threaded diagnostic patch capture with logical guest execution flags and nonexecuting native backing. Not AOT publication or executable patch behavior.'),indent=2)+'\n');print(run.stdout.strip())
