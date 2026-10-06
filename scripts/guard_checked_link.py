"""Guard unresolved library boundaries of a concrete checked link set."""
import hashlib,json,os,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
def guard_link(entries,destination,*,target='arm64-apple-macos12'):
 destination.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');plugin=R/'generated/tools/external-memory-pass/ExternalMemoryPass.dylib';pm=json.loads(plugin.with_name('result.json').read_text());assert sha(plugin)==pm['plugin_sha256'] and sha(R/'port/compiler/ExternalMemoryPass.cpp')==pm['source_sha256'];assert all(sha(pathlib.Path(n))==h for n,h in pm['tools_sha256'].items())
 names=set()
 for entry in entries:
  obj=pathlib.Path(entry['object']);assert sha(obj)==entry['object_sha256']
  if entry['bitcode'] is None:continue
  output=subprocess.check_output([str(tools/'llvm-nm'),'--defined-only','--extern-only','--format=posix',str(obj)],text=True)
  for line in output.splitlines():
   fields=line.split()
   if len(fields)>=2 and fields[1] in ('T','W'):assert fields[0].startswith('_');names.add(fields[0][1:])
 config=R/'config/runtime/external-memory-contracts.json';policy=json.loads(config.read_text());assert all(sha(R/n)==h for n,h in policy.get('reviewed_sources',{}).items());policy['checked_symbols']=sorted(names);policy['font_sources']=[str(R/p) for p in policy.get('font_sources',[])];policy['scheduling_sources']=[str(R/p) for p in policy.get('scheduling_sources',[])];policy['processor_snapshot_sources']=[str(R/p) for p in policy.get('processor_snapshot_sources',[])];policy['memory_manager_sources']=[str(R/p) for p in policy['memory_manager_sources']];policy['barrier_asm_sources']=[str(R/p) for p in policy['barrier_asm_sources']];policy_path=destination/'policy.json';policy_path.write_text(json.dumps(policy,indent=2)+'\n');environment=dict(os.environ,TRIBES_EXTERNAL_POLICY=str(policy_path));report=destination/'result.json';previous=json.loads(report.read_text()) if report.exists() else {};state=dict(state='building',target=target,subpages_enabled=False,config_sha256=sha(config),policy_sha256=sha(policy_path),plugin_build_sha256=sha(plugin.with_name('result.json')),script_sha256=sha(pathlib.Path(__file__).resolve()),objects=[]);report.write_text(json.dumps(state,indent=2)+'\n');outputs=[]
 def run(command,log):
  with log.open('w') as stream:
   process=subprocess.Popen(command,env=environment,stdout=stream,stderr=stream);state.update(state='running',pid=process.pid,command=command);report.write_text(json.dumps(state,indent=2)+'\n');code=process.wait()
  state.update(state='step_finished' if code==0 else 'failed',exit=code);report.write_text(json.dumps(state,indent=2)+'\n');assert not code,log
 for index,entry in enumerate(entries):
  if entry['bitcode'] is None:outputs.append(entry['object']);state['objects'].append(dict(entry,trusted=True));continue
  src=pathlib.Path(entry['bitcode']);assert sha(src)==entry['bitcode_sha256'];name='%03d-%s'%(index,src.stem);bc=destination/(name+'.bc');obj=destination/(name+'.o')
  old=previous.get('objects',[]);cached=old[index] if index<len(old) else {}
  if previous.get('target','arm64-apple-macos12')==target and previous.get('state')=='compiled' and previous.get('policy_sha256')==state['policy_sha256'] and previous.get('plugin_build_sha256')==state['plugin_build_sha256'] and cached.get('input')==entry and obj.exists() and bc.exists() and cached.get('object_sha256')==sha(obj) and cached.get('bitcode_sha256')==sha(bc):
   outputs.append(str(obj));state['objects'].append(dict(cached,reused_verified_bitcode=True));continue
  run([str(tools/'opt'),'-load-pass-plugin',str(plugin),'-passes=tribes-external-memory,verify',str(src),'-o',str(bc)],destination/(name+'-pass.log'))
  census=json.loads(next(line.split(' ',1)[1] for line in (destination/(name+'-pass.log')).read_text().splitlines() if line.startswith('TRIBES_EXTERNAL_PASS ')))
  run([str(tools/'clang'),'-target',target,'-O0','-Wno-override-module','-c',str(bc),'-o',str(obj)],destination/(name+'-compile.log'))
  outputs.append(str(obj));state['objects'].append(dict(input=entry,object=str(obj),object_sha256=sha(obj),bitcode_sha256=sha(bc),census=census))
 state.update(state='compiled',scope='All direct external calls are checked-link symbols, bracketed buffer operations, explicit host-only or memory-owner contracts, or pre-call stops. Indirect calls require registered checked code. Missing contracts remain runtime stops. No subpage enablement in this build step.');report.write_text(json.dumps(state,indent=2)+'\n');return outputs,sha(report)
