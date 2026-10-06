#!/usr/bin/env python3
"""Exercise published-byte immutability using real instrumented native writes."""
import hashlib,json,pathlib,subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/published-code';out.mkdir(exist_ok=True)
source=R/'tests/published_code_store.c'
objects,checked=compile_support([source],out/'checked',[])
sources=[R/'tests/published_code_test.c',R/'port/core/guest_memory_checks.c']
subprocess.run(['clang','-target','arm64-apple-macos12','-O2',*map(str,sources),*objects,'-o',str(out/'test')],check=True)
run=subprocess.run([str(out/'test')],text=True,capture_output=True)
(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr)
assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
assert run.stderr.count('write to published AOT code')==4
dispatch_sources=[R/'tests/published_dispatch_test.c',R/'port/core/guest_dispatch.c']
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT',*map(str,dispatch_sources),'-o',str(out/'dispatch-test')],check=True)
dispatch=subprocess.run([str(out/'dispatch-test')],capture_output=True,text=True);assert dispatch.returncode==0 and dispatch.stderr.count('unpublished original guest PC')==2
(out/'dispatch.log').write_text(dispatch.stdout+dispatch.stderr)
report=dict(passed=True,checked_support_sha256=checked,binary_sha256=sha(out/'test'),dispatch_binary_sha256=sha(out/'dispatch-test'),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+dispatch_sources+[source,R/'port/core/guest_memory_checks.h',pathlib.Path(__file__).resolve()]},scope='One-time atomic expected-byte validation and immutable exact intervals; failed checks do not publish, caller metadata is copied, neighboring bytes remain writable, and four real compiler-instrumented writes stop before changing shared memory. Linked lookup policy rejects both engine and bank before publication while DLL/host-return remain available. No game patch route activation.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
