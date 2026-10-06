"""Compile the supplemental bootstrap sources through the memory-check pass."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
def compile_support(sources,destination,flags,*,target='arm64-apple-macos12',sdk_name='macosx'):
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');plugin=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib';plugin_meta=plugin.with_name('result.json');meta=json.loads(plugin_meta.read_text());assert meta['plugin_sha256']==sha(plugin) and meta['source_sha256']==sha(R/'port/compiler/GuestMemoryPass.cpp');assert all(sha(pathlib.Path(n))==h for n,h in meta['tools_sha256'].items())
 sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();flags=['-isysroot',sdk]+flags
 destination.mkdir(exist_ok=True);report=destination/'result.json';previous=json.loads(report.read_text()) if report.exists() else {};state=dict(state='building',target=target,sdk=sdk,coverage_complete=False,plugin_build_sha256=sha(plugin_meta),script_sha256=sha(pathlib.Path(__file__).resolve()),objects={});objects=[]
 def run(cmd,log):
  state['command']=cmd
  with log.open('w') as stream:
   process=subprocess.Popen(cmd,stdout=stream,stderr=stream);state.update(state='running',pid=process.pid);report.write_text(json.dumps(state,indent=2)+'\n');code=process.wait()
  state.update(state='step_finished' if code==0 else 'failed',exit=code);report.write_text(json.dumps(state,indent=2)+'\n');assert code==0,log
 for index,source in enumerate(sources):
  name='%02d-%s'%(index,source.stem);llvm=destination/(name+'.ll');bc=destination/(name+'.bc');obj=destination/(name+'.o');trusted=source in [R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
  cached=previous.get('objects',{}).get(name,{})
  # LLVM inputs have no textual includes or frontend flags. Reuse only the
  # exact checked source, pinned pass/tool build, bitcode and object. C/ObjC
  # inputs always rebuild so changed headers cannot hide behind this cache.
  if source.suffix=='.ll' and previous.get('target','arm64-apple-macos12')==target and previous.get('state') in ('compiled','failed','step_finished') and previous.get('plugin_build_sha256')==sha(plugin_meta) and cached.get('source_sha256')==sha(source) and obj.exists() and bc.exists() and cached.get('object_sha256')==sha(obj) and cached.get('bitcode_sha256')==sha(bc):
   state['objects'][name]=dict(cached,reused_verified_llvm=True);objects.append(str(obj));continue
  if trusted:
   run([str(tools/'clang'),'-target',target,'-O2']+flags+['-c',str(source),'-o',str(obj)],destination/(name+'-compile.log'));census=None
  else:
   if source.suffix=='.ll':llvm=source
   else:run([str(tools/'clang'),'-target',target,'-O2','-S','-emit-llvm']+flags+[str(source),'-o',str(llvm)],destination/(name+'-frontend.log'))
   run([str(tools/'opt'),'-load-pass-plugin',str(plugin),'-passes=tribes-guest-memory,verify',str(llvm),'-o',str(bc)],destination/(name+'-pass.log'))
   census=json.loads(next(l.split(' ',1)[1] for l in (destination/(name+'-pass.log')).read_text().splitlines() if l.startswith('TRIBES_MEMORY_PASS ')))
   run([str(tools/'clang'),'-target',target,'-O0','-Wno-override-module','-c',str(bc),'-o',str(obj)],destination/(name+'-codegen.log'))
  state['objects'][name]=dict(source=str(source.relative_to(R)),source_sha256=sha(source),object_sha256=sha(obj),trusted_barrier_runtime=trusted,census=census,bitcode_sha256=sha(bc) if not trusted else None);objects.append(str(obj))
 state.update(state='compiled',scope='All selected supplementary C/LLVM objects instrumented except trusted guard runtime. External call/library contracts remain unproven; no subpage enablement.');report.write_text(json.dumps(state,indent=2)+'\n');return objects,sha(report)
