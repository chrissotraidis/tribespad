#!/usr/bin/env python3
"""Exercise emitted bodies at their current unserviced-exception boundary."""
import argparse,hashlib,json,pathlib,subprocess
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
    inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True))
    assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
profile=a.simulator_device or 'macos';prior=R/'generated/sse/vcvttsd2si-lowering'/profile
report=json.loads((prior/'result.json').read_text());assert report['pass'] and report['state']=='finished'
for name,h in report['sources_sha256'].items():assert sha(R/name)==h
assert sha(prior/'bodies.ll')==report['ir_sha256'] and sha(prior/'bodies.llasm')==report['emitted_body_sha256']
out=R/'generated/sse/vcvttsd2si-faults'/profile;out.mkdir(parents=True,exist_ok=True)
support=R/'port/llasm-support';platform='simulator' if a.simulator_device else 'macos';math=sse_inputs(platform);assert math['evidence']==report['sse_math']
sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip()
source=R/'tests/vcvttsd2si_fault_test.c';exe=out/'native'
cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(prior)]+math['compile_flags']+[str(prior/'bodies.ll'),str(source),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)]
subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120)
(out/'run.log').write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
result={'pass':True,'state':'finished','probes':128,'prior_report_sha256':sha(prior/'result.json'),'ir_sha256':sha(prior/'bodies.ll'),'metadata_sha256':sha(prior/'moves.inc'),'source_sha256':sha(source),'driver_sha256':sha(pathlib.Path(__file__)),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'Actual emitted REG/MEM all8 GPR destinations; unmasked invalid/precision, masked invalid with precision unmasked, masked precision with invalid unmasked, DAZ/exact conversion with all masks clear and prior sticky bits. Full CPU/memory preservation except actual masked result/status. SIGABRT boundary, not Windows fault delivery or physical x86 proof'}
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(run.stdout.strip())
