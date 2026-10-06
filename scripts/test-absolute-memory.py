#!/usr/bin/env python3
"""Compile the real absolute-memory helper with both checked runtime passes."""
import hashlib,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/absolute-memory';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
helper=R/'port/llasm-support/tribes_absolute_memory.c'
objects,support_sha=compile_support([helper],out/'support',['-std=c11'])
support=json.loads((out/'support/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=support['objects'][p.stem]
 entries.append(dict(object=str(p),object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guard_sha=guard_link(entries,out/'guarded')
sources=[R/'tests/absolute_memory_test.c',R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
exe=out/'test';subprocess.run(['clang','-target','arm64-apple-macos12','-std=c11','-O2','-Wall','-Wextra','-Werror','-pthread']+list(map(str,sources))+objects+['-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
(out/'run.log').write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,output=run.stdout.strip(),support_sha256=support_sha,guard_sha256=guard_sha,binary_sha256=sha(exe),source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[helper,helper.with_suffix('.h'),pathlib.Path(__file__).resolve()]},scope='Checked absolute guest-memory helpers; native macOS ARM64 diagnostic. No executable patch publication or Windows SEH parity.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
