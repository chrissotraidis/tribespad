#!/usr/bin/env python3
"""Run the condition service fixture on exactly one already-booted simulator."""
import argparse,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('device');a=ap.parse_args()
inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True))
devices=[d for rows in inventory['devices'].values() for d in rows if d['state']=='Booted'];assert len(devices)==1 and devices[0]['udid']==a.device
root=R/'generated/integration-review/condition-variables';proof=json.loads((root/'result.json').read_text());assert proof['passed']
for name,h in proof['sources_sha256'].items():assert sha(R/name)==h,name
out=root/'simulator';out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps(dict(passed=False,state='running'))+'\n')
sources=[R/'port/core'/n for n in ['guest_condition.c','guest_srw.c','critical_section.c','guest_heap.c','guest_memory_checks.c','guest_external_checks.c']]+[R/'tests/condition_test.c',R/'tests/fixtures/condition_dispatch.c',R/'tests/fixtures/native_enter.ll',root/'bindings.ll']
sdk=subprocess.check_output(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],text=True).strip();exe=out/'condition-test'
cmd=['clang','-target','arm64-apple-ios16-simulator','-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wno-override-module','-Wall','-Wextra','-Werror',*map(str,sources),'-o',str(exe)]
subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.device,str(exe)],capture_output=True,text=True,timeout=90);(out/'run.log').write_text(run.stdout+run.stderr)
assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,state='finished',device=devices[0]['name'],udid=a.device,command=cmd,stdout=run.stdout,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources},host_proof_sha256=sha(root/'result.json'),script_sha256=sha(pathlib.Path(__file__)),scope='Actual single iPad Simulator execution of the condition-variable ABI fixture, native concurrency and timeouts. No UIKit/original-game lifecycle, rendered menu, touch controls, physical-device or authenticated-gameplay acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
