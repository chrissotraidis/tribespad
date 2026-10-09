#!/usr/bin/env python3
"""Prove skipping the throwaway object preserves the guarded native output."""
import hashlib,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/deferred-codegen';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=out/'input.ll'
source.write_text('''target datalayout = "e-p:64:64-f64:64:64-n32:64"
define i32 @read_value(ptr %p) {
 %v = load i32, ptr %p
 ret i32 %v
}
define i32 @forward_value(ptr %p) {
 %v = call i32 @read_value(ptr %p)
 ret i32 %v
}
''')
results={}
for platform,target,sdk in [('macos','arm64-apple-macos12','macosx'),('simulator','arm64-apple-ios16-simulator','iphonesimulator'),('device','arm64-apple-ios16','iphoneos')]:
 rows=[]
 for deferred in [False,True]:
  work=out/(platform+('-deferred' if deferred else '-original'));work.mkdir(exist_ok=True)
  inputs,_=compile_support([source],work/'support',[],target=target,sdk_name=sdk,defer_codegen=deferred)
  p=pathlib.Path(inputs[0]);bc=p.with_suffix('.bc')
  assert p.suffix==('.bc' if deferred else '.o')
  if deferred:assert not list((work/'support').glob('*.o'))
  entry=dict(object=str(p),object_sha256=sha(p),bitcode=str(bc),bitcode_sha256=sha(bc))
  objects,_=guard_link([entry],work/'guarded',target=target)
  meta=json.loads((work/'guarded/result.json').read_text())
  assert json.loads((work/'guarded/policy.json').read_text())['checked_symbols']==['forward_value','read_value']
  rows.append(dict(bitcode=meta['objects'][0]['bitcode_sha256'],native=sha(pathlib.Path(objects[0])),object=objects[0]))
  if deferred:
   again,_=compile_support([source],work/'support',[],target=target,sdk_name=sdk,defer_codegen=True)
   cached=json.loads((work/'support/result.json').read_text())
   assert again==inputs and cached['objects'][p.stem]['reused_verified_llvm']
   damaged=dict(entry,bitcode_sha256='0'*64)
   try:guard_link([damaged],work/'reject-damaged',target=target)
   except AssertionError:pass
   else:raise AssertionError('Corrupt checked input was accepted')
 assert rows[0]['bitcode']==rows[1]['bitcode'] and rows[0]['native']==rows[1]['native'],rows
 results[platform]=rows[1]
# Execute the final object against the real permission runtime: an allowed read
# works, and a denied read still stops before returning through the wrapper.
harness=out/'main.c'
harness.write_text('''#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "guest_memory_checks.h"
extern int forward_value(const int *);
int main(void){
 int *p=mmap(0,65536,PROT_READ|PROT_WRITE,MAP_ANON|MAP_SHARED,-1,0);
 assert(p!=MAP_FAILED);*p=42;assert(tribes_memory_checks_configure((uintptr_t)p,65536));
 assert(forward_value(p)==42);assert(tribes_memory_permissions_set(0,4096,0));
 pid_t child=fork();assert(child>=0);
 if(!child){struct rlimit lim={0,0};setrlimit(RLIMIT_CORE,&lim);alarm(5);freopen("/dev/null","w",stderr);forward_value(p);_exit(99);}
 int status;assert(waitpid(child,&status,0)==child);assert(WIFSIGNALED(status)&&WTERMSIG(status)==SIGABRT);
 puts("PASS: identical guarded IR/native output on Mac, Simulator and device; verified cache, corrupt input rejection, permitted read and denied-read fault");
}
''')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
exe=out/'test'
subprocess.run(['clang','-isysroot',sdk,'-O2','-I',str(R/'port/core'),str(harness),results['macos']['object'],str(R/'port/core/guest_memory_checks.c'),str(R/'port/core/guest_external_checks.c'),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,output=run.stdout.strip(),platforms=results,sources_sha256={str(p.relative_to(R)):sha(p) for p in [R/'scripts/checked_support.py',R/'scripts/guard_checked_link.py',R/'scripts/build-ipad-engine.py',pathlib.Path(__file__).resolve()]})
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
