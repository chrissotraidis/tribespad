#!/usr/bin/env python3
"""Checked dynamic VM semantics through emitted memory accesses, one simulator at a time."""
import argparse,hashlib,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for rows in d['devices'].values() for x in rows if x['state']=='Booted']==[a.simulator_device]
out=R/'generated/integration-review/vm-subpages'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk','iphonesimulator' if a.simulator_device else 'macosx','--show-sdk-path'],text=True).strip()
sources=[R/'port/core/guest_virtual_memory.c',R/'port/core/guest_heap.c',R/'tests/fixtures/vm_subpage_access.c'];trusted=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/vm_subpages_test.c'
objects,checked=compile_support(sources,out/'checked',[],target=target,sdk_name='iphonesimulator' if a.simulator_device else 'macosx')
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=target)
access=objects[-1];runs=[]
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
 exe=out/('test-'+mode);cmd=['clang','-target',target,'-isysroot',sdk,'-O1','-g','-pthread','-Wno-override-module','-Wall','-Wextra','-Werror']
 if mode=='tsan':cmd+=['-fsanitize=thread']
 cmd+=list(map(str,trusted))+[str(test)]+(objects if mode=='checked' else list(map(str,sources[:-1]))+[access]);subprocess.run(cmd+['-o',str(exe)],check=True)
 command=['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)]
 run=subprocess.run(command,capture_output=True,text=True,timeout=120);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(mode,run.returncode,run.stdout,run.stderr)
 faults=[]
 if mode!='tsan':
  for case in ['padding','released','reserved','readonly','cross-page','recommit-readonly','decommitted']:
   r=subprocess.run(command+[case],capture_output=True,text=True,timeout=30);(out/(mode+'-'+case+'.log')).write_text(r.stdout+r.stderr);assert r.returncode==(134 if a.simulator_device else -6) and 'checked guest memory violation' in r.stderr,(case,r.returncode,r.stderr);faults.append(case)
 runs.append(dict(mode=mode,passed=True,stdout=run.stdout,faults=faults,binary_sha256=sha(exe)));print(mode,run.stdout.strip(),flush=True)
paths=sources+trusted+[test,pathlib.Path(__file__).resolve(),R/'port/core/guest_virtual_memory.h',R/'port/core/guest_memory_checks.h',R/'config/runtime/external-memory-contracts.json']
(out/'result.json').write_text(json.dumps(dict(passed=True,state='finished',runs=runs,checked_sha256=checked,guarded_sha256=guarded,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Actual dynamic VM commitment and emitted access guards on Mac or sole simulator. Native-only geometry rejection remains separately tested. Not original-game or physical-device acceptance.'),indent=2)+'\n')
