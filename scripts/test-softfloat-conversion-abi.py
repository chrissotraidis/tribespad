#!/usr/bin/env python3
"""Reject wrong direct conversion ABIs before calling the actual math library."""
import hashlib,json,os,pathlib,subprocess
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/conversion-contract';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');plugin=R/'generated/tools/external-memory-pass/ExternalMemoryPass.dylib';pm=json.loads(plugin.with_name('result.json').read_text());assert sha(plugin)==pm['plugin_sha256'] and sha(R/'port/compiler/ExternalMemoryPass.cpp')==pm['source_sha256']
memory=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib';mm=json.loads(memory.with_name('result.json').read_text());assert sha(memory)==mm['plugin_sha256'] and sha(R/'port/compiler/GuestMemoryPass.cpp')==mm['source_sha256']
policy=json.loads((R/'config/runtime/external-memory-contracts.json').read_text());policy['checked_symbols']=[];policy['memory_manager_sources']=[];policy['barrier_asm_sources']=[];(out/'policy.json').write_text(json.dumps(policy,indent=2)+'\n')
env=dict(os.environ,TRIBES_EXTERNAL_POLICY=str(out/'policy.json'));math=sse_inputs('macos')
main=out/'main.c';main.write_text('#include "'+str(R/'port/core/guest_external_checks.h')+'"\nextern void bad_entry(void);\nint main(void){tribes_memory_external_configure(0x100000,65536);tribes_memory_external_enable();bad_entry();return 99;}\n')
cases=[('f32_to_extF80','i64','i64','i64 1'),('ui64_to_extF80','[2 x i64]','i32','i32 1'),('extF80_to_f64','i64','[3 x i64]','[3 x i64] zeroinitializer'),('extF80_to_f32','i64','[2 x i64]','[2 x i64] zeroinitializer'),('f64_to_extF80','[2 x i64]','ptr','ptr null'),('extF80_roundToInt','[2 x i64]','[2 x i64], i8, i1','[2 x i64] zeroinitializer, i8 0, i1 false')]
results=[]
for index,(name,result,args,values) in enumerate(cases):
 source=out/('%d.ll'%index);guarded=out/('%d-guarded.ll'%index);binary=out/('test-%d'%index)
 source.write_text('declare %s @%s(%s)\ndefine void @bad_entry() { %%v = call %s @%s(%s)\n ret void\n}\n'%(result,name,args,result,name,values))
 subprocess.run([str(tools/'opt'),'-load-pass-plugin',str(memory),'-load-pass-plugin',str(plugin),'-passes=tribes-guest-memory,tribes-external-memory,verify','-S',str(source),'-o',str(guarded)],env=env,check=True,capture_output=True)
 subprocess.run(['clang','-target','arm64-apple-macos12','-O0','-Wno-override-module',str(main),str(guarded),str(R/'port/core/guest_external_checks.c'),str(R/'port/core/guest_memory_checks.c'),*math['libraries'],'-o',str(binary)],check=True,capture_output=True)
 run=subprocess.run([str(binary)],capture_output=True,text=True);assert run.returncode==-6 and 'unverified call: '+name in run.stderr,(index,run.returncode,run.stderr)
 results.append(dict(name=name,result=result,arguments=args,binary_sha256=sha(binary),stop=run.stderr.strip()))
(out/'result.json').write_text(json.dumps(dict(passed=True,cases=results,plugin_sha256=sha(plugin),math=math['evidence'],script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Six independent direct-call declarations reject mismatched scalar/aggregate/result/pointer ABIs and an unapproved extended rounding function before invoking the actual linked library. Positive conversion behavior is exercised by the external-memory-pass fixture.'),indent=2)+'\n');print('PASS: six direct conversion ABI/contract rejection cases')
