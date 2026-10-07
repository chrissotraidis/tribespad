#!/usr/bin/env python3
"""Exercise the four adapted original renderer calls with real native workers."""
import hashlib,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
from render_job_runtime_inputs import adapt,ROWS
from renderer_batch_runtime_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/render-job-handoff';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
source=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/module.ll';original=source.read_text();adapted,records=adapt(original)
# Exact pinning must reject an altered original caller before emitting an adapter.
for row in ROWS:
 body=definition(original,row[0]);bad=original.replace(body,body.replace('align 4','align 1',1),1)
 try:adapt(bad)
 except AssertionError:pass
 else:raise AssertionError('changed original caller accepted')
types=next(l for l in original.splitlines() if l.startswith('%_cpu = type'))
text=types+'\ndeclare void @tribes_render_job_step(%_cpu*, i32)\n'
for label,row in zip(['begin','complete','wait','finish'],ROWS):
 text+=definition(adapted,row[0])+f'\ndefine hidden fastcc void @{row[2]}(%_cpu* %cpu) {{ ret void }}\ndefine void @test_{label}(%_cpu* %cpu) {{ call fastcc void @{row[0]}(%_cpu* %cpu)\n ret void }}\n'
ir=out/'calls.ll';ir.write_text(text)
init=out/'init.c';init.write_text('#define main unused_mission_main\n#include "'+str(R/'tests/mission_literal_test.c')+'"\n')
helper=R/'port/core/guest_render_job.c';fixture=R/'tests/render_job_test.c';barrier=R/'port/core/guest_memory_checks.c';external=R/'port/core/guest_external_checks.c'
objects,_=compile_support([helper,ir],out/'checked',['-DPTROFS_64BIT']);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
guarded,_=guard_link(entries,out/'guarded')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['clang','-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror','-pthread']
runs=[]
for mode in ['normal','checked','tsan']:
 exe=out/(mode+'-test');inputs=guarded if mode=='checked' else [str(helper),str(ir)]
 subprocess.run(flags+(['-fsanitize=thread'] if mode=='tsan' else [])+(['-DCHECKED'] if mode=='checked' else [])+[str(init),str(fixture),str(barrier),str(external),*inputs,'-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=90);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 faults=[]
 if mode=='checked':
  for fault in ['bad-phase','bad-step','readonly','dirty','early-complete','bad-stack','repeat-begin','early-finish','wrong-wait','bad-callback','bad-job','duplicate-complete','modified']:
   run=subprocess.run([str(exe),fault],capture_output=True,text=True,timeout=10);assert run.returncode==-6 and ('checked guest memory violation' if fault=='readonly' else 'invalid original render-job handoff') in run.stderr,(fault,run.returncode,run.stderr);faults.append(fault)
 runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),faults=faults));print(mode,'PASS',flush=True)
paths=[pathlib.Path(__file__).resolve(),R/'scripts/render_job_runtime_inputs.py',helper,fixture,R/'tests/mission_literal_test.c',barrier,external,R/'port/core/guest_memory_checks.h',R/'config/runtime/external-memory-contracts.json']
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',records=records,runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Four exact translated original call prefixes with checked native completion fence, real workers, 200 reusable before/after-wait cycles, data visibility, CPU/live-stack canaries and invalid handoff/memory guards. Fixture startup revisions test phase gating, not original startup.'),indent=2)+'\n')
