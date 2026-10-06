#!/usr/bin/env python3
"""Build a separately checked diagnostic IFC22 object and expose link-audit gaps.

This artifact is not enabled by the startup harness and does not attest helpers.
"""
import argparse,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('lift',type=pathlib.Path);a=ap.parse_args();native=a.lift.resolve()/'native-diagnostic';original=json.loads((native/'result.json').read_text());assert original['diagnostic_only'] and original['state']=='compiled'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();source=native/'module.ll';assert sha(source)==original['module_ll_sha256']
plugin=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib';build=json.loads(plugin.with_name('result.json').read_text());assert sha(plugin)==build['plugin_sha256'] and sha(R/'port/compiler/GuestMemoryPass.cpp')==build['source_sha256']
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');assert all(sha(pathlib.Path(n))==h for n,h in build['tools_sha256'].items())
out=native/'checked';out.mkdir(exist_ok=True);report=out/'result.json'
if report.exists():
 previous=json.loads(report.read_text())
 if previous.get('state')=='running':
  live=subprocess.run(['ps','-p',str(previous['pid']),'-o','command='],capture_output=True,text=True)
  assert live.returncode or str(out) not in live.stdout,'Existing checked compile is live; wait for it'
state=dict(state='preparing',diagnostic_only=True,playable=False,link_coverage_complete=False,source_sha256=sha(source),plugin_build_sha256=sha(plugin.with_name('result.json')),script_sha256=sha(pathlib.Path(__file__).resolve()));report.write_text(json.dumps(state,indent=2)+'\n')
def run(cmd,log):
 with log.open('w') as f:
  child=subprocess.Popen(cmd,stdout=f,stderr=f);state.update(state='running',pid=child.pid,command=cmd);report.write_text(json.dumps(state,indent=2)+'\n');code=child.wait()
 state.update(state='step_finished' if code==0 else 'failed',exit=code);report.write_text(json.dumps(state,indent=2)+'\n');assert code==0,log
bc=out/'module.bc';obj=out/'module.o'
run([str(tools/'opt'),'-load-pass-plugin',str(plugin),'-passes=tribes-guest-memory,verify',str(source),'-o',str(bc)],out/'pass.log')
census=json.loads(next(l.split(' ',1)[1] for l in (out/'pass.log').read_text().splitlines() if l.startswith('TRIBES_MEMORY_PASS ')));state['census']=census
run([str(tools/'clang'),'-target','arm64-apple-macos12','-O0','-Wno-override-module','-c',str(bc),'-o',str(obj)],out/'compile.log')
state.update(state='compiled',bitcode_sha256=sha(bc),object_sha256=sha(obj),object_bytes=obj.stat().st_size,scope='Real diagnostic DLL image memory accesses bracketed by build-time checks. Native helpers, wrappers, native libraries and external calls still require a link-level coverage audit before startup can enable software subpage protection. Not a production translation or initialized game.');report.write_text(json.dumps(state,indent=2)+'\n');print(json.dumps({k:v for k,v in state.items() if k not in ['command','pid','census']},indent=2));print('Memory operations:',{k:v for k,v in census.items() if k!='calls_requiring_link_audit'});print('External call targets requiring audit:',len(census['calls_requiring_link_audit']))
