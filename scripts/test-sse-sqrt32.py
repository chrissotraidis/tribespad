#!/usr/bin/env python3
"""Verify the unary arithmetic adapter, without SRW lowering or CPU publication."""
import argparse,hashlib,json,pathlib,random,subprocess
from f32_sqrt_reference import root
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
    inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True))
    assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
platform='simulator' if a.simulator_device else 'macos'
out=R/'generated/sse/sqrt32-adapter'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
dep=next(d for d in json.loads((R/'dependencies.lock.json').read_text())['sources'] if d['name']=='softfloat')
assert sha(R/'ref/softfloat/SoftFloat-3e.zip')==dep['sha256']
sf=R/'generated/softfloat'/platform;sf_report=json.loads((sf/'result.json').read_text())
assert sf_report['thread_local'] and sf_report['specialization']=='8086' and sf_report['source_sha256']==dep['sha256'] and sf_report['library_sha256']==sha(sf/'softfloat.a')
values={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,0x3f800000,0xbf800000,0x40000000,0x40800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc12345,0xffc12345,0x7fa12345,0xffa12345,0x7f800001,0xff800001}
for exponent in range(255):
    for fraction in [0,1,2,0x1fffff,0x200000,0x3fffff,0x400000,0x7ffffe,0x7fffff]:
        values.add((exponent<<23)|fraction)
rng=random.Random(25034);values.update(rng.getrandbits(32) for _ in range(2048))
cases=[]
for source in sorted(values):
    for bits in range(16):
        for sticky in [0,1,31,63]:
            control=0x1f80|((bits&3)<<13)|(0x40 if bits&4 else 0)|(0x8000 if bits&8 else 0)|sticky
            result,raised=root(source,control);cases.append((source,control,result,raised))
request=''.join(f'{source:08x} {control:04x}\n' for source,control,_,_ in cases)
(out/'requests.txt').write_text(request)
oracle_source=R/'tests/sse_sqrt32_oracle.c';oracle=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(oracle_source),'-o',str(oracle)],check=True)
run=subprocess.run(['arch','-x86_64',str(oracle)],input=request,text=True,capture_output=True,check=True,timeout=120)
(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(cases)
for index,(case,reply) in enumerate(zip(cases,replies)):
    source,control,result,raised=case;actual,status=[int(x,16) for x in reply.split()]
    assert (actual,status)==(result,control|raised),(index,case,reply)
table=out/'cases.inc';table.write_text('static const Case cases[]={\n'+',\n'.join('{'+','.join(f'0x{x:x}u' for x in case)+'}' for case in cases)+'\n};\n')
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
sources=[R/'port/core/sse_arithmetic.c',R/'tests/sse_sqrt32_test.c'];include=R/dep['path']/'source/include';runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
    exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-std=c11','-O2','-pthread','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-Wall','-Wextra','-Werror','-I',str(out),'-I',str(include)]
    if mode=='tsan':cmd+=['-fsanitize=thread']
    run=subprocess.run(cmd+list(map(str,sources))+[str(sf/'softfloat.a'),'-o',str(exe)],capture_output=True,text=True)
    (out/(mode+'-compile.log')).write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
    run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120)
    (out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
    runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':sha(exe)})
paths=sources+[R/'port/core/sse_arithmetic.h',oracle_source,R/'scripts/f32_sqrt_reference.py',pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'cases':len(cases),'input_values':len(values),'corpus_sha256':sha(table),'request_sha256':sha(out/'requests.txt'),'reference_sha256':sha(out/'reference.txt'),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'softfloat_archive_sha256':dep['sha256'],'softfloat_library_sha256':sha(sf/'softfloat.a'),'runs':runs,'scope':'Unary binary32 candidate/status adapter versus exact integer sqrt and repeated-lane Rosetta VSQRTPS; eight threads and four TLS fields restored; candidate API only, no emitted instruction, CPU publication, precise fault delivery, physical Intel or gameplay claim'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
