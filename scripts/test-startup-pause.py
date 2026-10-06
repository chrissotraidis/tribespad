#!/usr/bin/env python3
"""Verify the reached PAUSE translation and its narrow native hint contract."""
import hashlib,json,os,pathlib,subprocess
from patch_runtime_inputs import translate_startup_pause
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/startup-pause';out.mkdir(parents=True,exist_ok=True)
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin')
image=R/'ref/qol/IFC22.dll';needle='call void @halopad_trap_unimplemented(i32 268827837) nounwind'
fixture='target triple = "arm64-apple-macos12"\ndefine void @pause_fixture(ptr %cpu) {\n '+needle+'\n ret void\n}\n'
translated=translate_startup_pause(fixture,image)
for bad in [fixture.replace(needle,''),fixture+needle,fixture+'\n; @llvm.aarch64.hint']:
 try:translate_startup_pause(bad,image)
 except AssertionError:pass
 else:raise AssertionError('accepted changed instruction site')
mutated=out/'mutated.dll';b=bytearray(image.read_bytes());b[-1]^=1;mutated.write_bytes(b)
try:
 try:translate_startup_pause(fixture,mutated)
 except AssertionError:pass
 else:raise AssertionError('accepted changed original image')
finally:mutated.unlink()
plugins=[R/'generated/tools'/d/(n+'.dylib') for d,n in [('guest-memory-pass','GuestMemoryPass'),('external-memory-pass','ExternalMemoryPass')]]
policy=R/'config/runtime/external-memory-contracts.json';config=json.loads(policy.read_text());config['checked_symbols']=[]
fixture_policy=out/'policy.json';fixture_policy.write_text(json.dumps(config));env=dict(os.environ,TRIBES_EXTERNAL_POLICY=str(fixture_policy))
base=[str(tools/'opt'),*[v for p in plugins for v in ['-load-pass-plugin',str(p)]],'-passes=tribes-guest-memory,tribes-external-memory,verify','-S']
for name,value in [('yield','1'),('wait','2')]:
 src=out/(name+'.ll');src.write_text(translated.replace('hint(i32 1)','hint(i32 '+value+')'));dst=out/(name+'-guarded.ll')
 run=subprocess.run(base+[str(src),'-o',str(dst)],env=env,capture_output=True,text=True)
 if name=='wait':
  assert run.returncode and 'Only state-preserving' in run.stderr
  (out/(name+'-pass.log')).write_text(run.stderr)
  continue
 assert not run.returncode,run.stderr
 (out/(name+'-pass.log')).write_text(run.stderr)
 ir=dst.read_text();assert 'i32 '+('1' if name=='yield' else '0')+')' in next(l for l in ir.splitlines() if 'call void @tribes_memory_external_gate' in l)
# Hints require an immediate; reject a dynamic operand before native codegen.
src=out/'dynamic.ll';src.write_text(translated.replace('ptr %cpu','i32 %hint').replace('hint(i32 1)','hint(i32 %hint)'))
run=subprocess.run(base+[str(src),'-o',str(out/'dynamic-guarded.ll')],env=env,capture_output=True,text=True);assert run.returncode and ('immarg' in run.stderr or 'Only state-preserving' in run.stderr)
(out/'dynamic-pass.log').write_text(run.stderr)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
obj=out/'yield.o';subprocess.run([str(tools/'clang'),'-target','arm64-apple-macos12','-c',str(out/'yield-guarded.ll'),'-o',str(obj)],check=True)
dis=subprocess.check_output([str(tools/'llvm-objdump'),'-d',str(obj)],text=True);assert '\tyield' in dis;(out/'disassembly.txt').write_text(dis)
driver=out/'driver.c';driver.write_text('''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "guest_external_checks.h"
extern void pause_fixture(void *);
int main(void){unsigned char cpu[440],before[440];
tribes_memory_external_configure((uintptr_t)cpu,sizeof cpu);tribes_memory_external_enable();
for(unsigned n=0;n<256;n++){for(unsigned i=0;i<440;i++)cpu[i]=(unsigned char)(i*17+n);memcpy(before,cpu,440);pause_fixture(cpu);assert(!memcmp(cpu,before,440));}
puts("PASS: PAUSE/YIELD preserves all 440 guest CPU bytes across 256 patterns");}
''')
exe=out/'test';subprocess.run(['clang','-isysroot',sdk,'-I',str(R/'port/core'),str(driver),str(obj),str(R/'port/core/guest_external_checks.c'),str(R/'port/core/guest_memory_checks.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);assert not run.returncode,(run.returncode,run.stderr)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps(dict(passed=True,cpu_patterns=256,source_rejections=4,invalid_hint_rejections=2,output=run.stdout,source_sha256={str(p.relative_to(R)):sha(p) for p in [image,pathlib.Path(__file__).resolve(),R/'scripts/patch_runtime_inputs.py',R/'port/compiler/ExternalMemoryPass.cpp',policy]},binary_sha256=sha(exe)),indent=2)+'\n');print(run.stdout.strip())
