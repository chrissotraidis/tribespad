#!/usr/bin/env python3
"""Native scalar SSE arithmetic adapter versus an independent x86 reference.
Does not execute emitted SRW instructions or publish results into guest CPUs.
"""
import argparse,hashlib,json,pathlib,random,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
platform='simulator' if a.simulator_device else 'macos'
out=R/'generated/sse/arithmetic'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
dep=next(d for d in json.loads((R/'dependencies.lock.json').read_text())['sources'] if d['name']=='softfloat')
assert sha(R/'ref/softfloat/SoftFloat-3e.zip')==dep['sha256']
sf=R/'generated/softfloat'/platform;sf_report=json.loads((sf/'result.json').read_text());assert sf_report['thread_local'] and sf_report['specialization']=='8086' and sf_report['source_sha256']==dep['sha256'] and sf_report['library_sha256']==sha(sf/'softfloat.a')
reference=R/'generated/sse/fp-oracles';old=json.loads((reference/'result.json').read_text());assert old['request_sha256']==sha(reference/'requests.txt')
cases=[]
for line in (reference/'requests.txt').read_text().splitlines():
 if line.split()[0] not in ('0','1','2'):continue
 fields=line.split();op=int(fields[0]);control=int(fields[1],16)&~0x3f;flags=int(fields[2],16);av,av1,bv,bv1=[int(s,16) for s in fields[3:]]
 cases.append((op,control,flags,av,av1,bv,bv1))
rng=random.Random(25034)
# Additional NaN payload priority, true-subtract zero sign, exact FTZ and
# working-precision carry versus later bounded rounding in scalar multiply.
double=[0,0x8000000000000000,1,0x8000000000000001,0x000fffffffffffff,0x0010000000000000,0x0010000000000001,0x3ff0000000000000,0xbff0000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ffa5a5a12345678,0xfffa5a5a12345678,0x7ff12345abcdeffe,0xfff12345abcdeffe]
single=[0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x00800001,0x00800002,0x3f7fffff,0x3f7ffffe,0x3f800000,0xbf800000,0x7f800000,0xff800000,0x7fc12345,0xffc56789,0x7fa12345,0xffa56789]
for op in range(3):
 values=single if op==2 else double
 for av in values:
  for bv in values:
   for control_bits in range(16):
    control=0x1f80|((control_bits&3)<<13)|(0x40 if control_bits&4 else 0)|(0x8000 if control_bits&8 else 0)
    a0=av|(rng.getrandbits(32)<<32) if op==2 else av
    b0=bv|(rng.getrandbits(32)<<32) if op==2 else bv
    cases.append((op,control,rng.choice([0,0x8d5,0x41,0x884]),a0,rng.getrandbits(64),b0,rng.getrandbits(64)))
request=''.join(f'{op} {m:x} {f:x} {av:016x} {av1:016x} {bv:016x} {bv1:016x}\n' for op,m,f,av,av1,bv,bv1 in cases);(out/'requests.txt').write_text(request)
oracle_source=R/'tests/sse_fp_oracle.c';oracle=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-O2','-mno-red-zone','-Wall','-Wextra','-Werror',str(oracle_source),'-o',str(oracle)],check=True)
run=subprocess.run(['arch','-x86_64',str(oracle)],input=request,text=True,capture_output=True,check=True,timeout=60);(out/'oracle-results.txt').write_text(run.stdout)
replies=run.stdout.splitlines();assert len(replies)==len(cases);rows=[];counts={}
for case,reply in zip(cases,replies):
 op,m,f,av,av1,bv,bv1=case;result,upper,integer,after_m,after_f=[int(s,16) for s in reply.split()]
 assert upper==av1 and integer==0 and after_f==f and after_m&~0x3f==m
 if op==2:assert result>>32==av>>32;result&=0xffffffff
 counts[op]=counts.get(op,0)+1
 rows.append('{'+f'{op}u,{m}u,'+','.join(f'UINT64_C(0x{v:x})' for v in (av,bv,result))+f',{after_m&0x3f}u'+'}')
table=out/'cases.inc';table.write_text('static const Case cases[]={\n'+',\n'.join(rows)+'\n};\n')
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
sources=[R/'port/core/sse_arithmetic.c',R/'tests/sse_arithmetic_test.c'];include=R/dep['path']/'source/include';runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-std=c11','-O2','-pthread','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-Wall','-Wextra','-Werror','-I',str(out),'-I',str(include)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 run=subprocess.run(cmd+list(map(str,sources))+[str(sf/'softfloat.a'),'-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':sha(exe)})
paths=sources+[R/'port/core/sse_arithmetic.h',oracle_source,pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'cases':len(cases),'operations':counts,'corpus_sha256':sha(table),'request_sha256':sha(out/'requests.txt'),'oracle_results_sha256':sha(out/'oracle-results.txt'),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'softfloat_archive_sha256':dep['sha256'],'softfloat_library_sha256':sha(sf/'softfloat.a'),'runs':runs,'scope':'Native masked scalar arithmetic adapter and full SoftFloat TLS restoration on eight threads; independent Rosetta reference, not physical Intel proof; no CPU destination publication, SRW lowering, DLL/gameplay or unmasked delivery claim'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
