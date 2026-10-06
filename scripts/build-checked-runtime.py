#!/usr/bin/env python3
"""Instrument existing native Win32 objects and report remaining external calls.

No coverage-complete claim or subpage enablement follows from successful builds.
"""
import hashlib,json,pathlib,subprocess
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();source_build=R/'generated/native-win32/macos';source_meta=json.loads((source_build/'result.json').read_text());out=R/'generated/native-win32/checked-macos';out.mkdir(parents=True,exist_ok=True);report=out/'result.json'
for name,digest in source_meta['system_messages_sha256'].items():assert sha(R/name)==digest,name
for name,digest in source_meta['ntstatus_sha256'].items():assert sha(R/name)==digest,name
for name,digest in source_meta['cpu_metadata_sha256'].items():assert sha(R/name)==digest,name
if report.exists():
 previous=json.loads(report.read_text())
 if previous.get('state')=='running':
  live=subprocess.run(['ps','-p',str(previous['pid']),'-o','command='],capture_output=True,text=True);assert live.returncode or str(out) not in live.stdout,'Existing checked build is live'
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');plugin=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib';build=json.loads(plugin.with_name('result.json').read_text());assert sha(plugin)==build['plugin_sha256'] and sha(R/'port/compiler/GuestMemoryPass.cpp')==build['source_sha256'];assert all(sha(pathlib.Path(n))==h for n,h in build['tools_sha256'].items())
state=dict(state='preparing',coverage_complete=False,source_build_sha256=sha(source_build/'result.json'),plugin_build_sha256=sha(plugin.with_name('result.json')),objects={},failures={});report.write_text(json.dumps(state,indent=2)+'\n');sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();math=sse_inputs('macos')
def run(cmd,log):
 with log.open('w') as stream:
  process=subprocess.Popen(cmd,stdout=stream,stderr=stream);state.update(state='running',pid=process.pid,command=cmd);report.write_text(json.dumps(state,indent=2)+'\n');code=process.wait()
 state.update(state='step_finished',exit=code);report.write_text(json.dumps(state,indent=2)+'\n');return code
for name,metadata in source_meta['objects'].items():
 paths=[R/'port/core'/(name+'.c'),R/'port/llasm-support'/(name+'.c'),R/'port/apple'/(name+'.m'),source_build/(name+'.ll')];found=[p for p in paths if p.exists()];assert len(found)==1;src=found[0];assert sha(src)==metadata['source_sha256'];original=out/(name+'.ll');bc=out/(name+'.bc');obj=out/(name+'.o')
 if src.suffix=='.ll':original=src
 else:
  cmd=[str(tools/'clang'),'-target','arm64-apple-macos12','-isysroot',sdk,'-O2','-S','-emit-llvm','-DPTROFS_64BIT',str(src),'-o',str(original)]
  if src.suffix=='.m':cmd+=['-fobjc-arc']
  if name=='sse_arithmetic':cmd+=math['compile_flags']
  if run(cmd,out/(name+'-frontend.log')):state['failures'][name]='frontend';continue
 # Finish ARC contraction before adding barriers; -O0 code generation does not
 # remove clang.arc.noop.use emitted by an optimized Objective-C frontend.
 pipeline=('function(objc-arc-contract),' if src.suffix=='.m' else '')+'tribes-guest-memory,verify'
 if run([str(tools/'opt'),'-load-pass-plugin',str(plugin),'-passes='+pipeline,str(original),'-o',str(bc)],out/(name+'-pass.log')):state['failures'][name]='pass';continue
 census=json.loads(next(l.split(' ',1)[1] for l in (out/(name+'-pass.log')).read_text().splitlines() if l.startswith('TRIBES_MEMORY_PASS ')))
 if run([str(tools/'clang'),'-target','arm64-apple-macos12','-isysroot',sdk,'-O0','-Wno-override-module','-c',str(bc),'-o',str(obj)],out/(name+'-codegen.log')):state['failures'][name]='codegen';continue
 state['objects'][name]=dict(source=str(src.relative_to(R)),source_sha256=sha(src),frontend_ir_sha256=sha(original),bitcode_sha256=sha(bc),object_sha256=sha(obj),census=census)
state.update(state='finished',exit=0 if not state['failures'] else 1,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Compiler coverage inventory for native Win32 services only. Support helpers, native libraries, external buffer calls and final-link coverage remain to be established. No software subpage protection is enabled.');report.write_text(json.dumps(state,indent=2)+'\n');print(json.dumps(dict(objects=len(state['objects']),failures=state['failures'],coverage_complete=False),indent=2))
