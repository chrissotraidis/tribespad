#!/usr/bin/env python3
"""Native masked UCOMISD helper against the recorded Rosetta x86 oracle."""
import argparse,hashlib,json,pathlib,subprocess
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
ref=R/'generated/sse/fp-oracles';reference=json.loads((ref/'result.json').read_text())
assert reference['state']=='complete' and reference['source_sha256']==sha(R/'tests/sse_fp_oracle.c') and reference['driver_sha256']==sha(R/'scripts/audit-sse-fp-oracles.py')
assert reference['request_sha256']==sha(ref/'requests.txt') and reference['rosetta_results_sha256']==sha(ref/'rosetta-results.txt')
out=R/'generated/sse/compare'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
rows=[]
requests=(ref/'requests.txt').read_text().splitlines();results=(ref/'rosetta-results.txt').read_text().splitlines();assert len(requests)==len(results)==reference['cases']
for q,r in zip(requests,results):
 if not q.startswith('5 '):continue
 m,f,left,a1,right,b1=[int(s,16) for s in q.split()[1:]]
 dst,dst1,integer,after_m,after_f=[int(s,16) for s in r.split()]
 assert (left,a1)==(dst,dst1) and integer==0
 rows.append('{'+','.join(f'UINT64_C(0x{v:x})' for v in (left,a1,right,b1))+','+','.join(f'0x{v:x}u' for v in (m,f,after_m,after_f))+'}')
table=out/'cases.inc';table.write_text('static const Case cases[]={\n'+',\n'.join(rows)+'\n};\n')
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
sources=[R/'port/llasm-support/tribes_sse.c',R/'tests/sse_compare_test.c'];runs=[]
math=sse_inputs('simulator' if a.simulator_device else 'macos')
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 run=subprocess.run(cmd+math['compile_flags']+list(map(str,sources))+math['sources']+math['libraries']+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':sha(exe)})
paths=sources+[R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'sse_math':math['evidence'],'target':target,'simulator_device':a.simulator_device,'cases':len(rows),'helper_forms':3,'equal_bit_self_aliases':True,'corpus_sha256':sha(table),'reference_report_sha256':sha(ref/'result.json'),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'runs':runs,'scope':'Native helper, masked IE/DE, low scalar lanes, full CPU guards, unaligned memory under AC-disabled profile; no emitted lowering, unmasked delivery, physical Intel/device or DLL execution claim'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
