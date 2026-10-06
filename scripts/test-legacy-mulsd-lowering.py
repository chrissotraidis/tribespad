#!/usr/bin/env python3
"""Execute emitted legacy ordered double comparison with full CPU expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/sse/legacy-mulsd-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;records=[]
pc=0x10007132;raw=bytes.fromhex('f20f590508ae1c10');assert pe.get_data(pc-0x10000000,len(raw))==raw
records.append(dict(pc=pc,bytes_hex=raw.hex(),kind=1,dest=0,source=0,address=4))
for dest in range(8):
 for src in range(8):records.append(dict(pc=None,bytes_hex=bytes([0xf2,0x0f,0x59,0xc0|dest<<3|src]).hex(),kind=0,dest=dest,source=src,address=0))
 for address,encoding in enumerate([bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x10]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')]):records.append(dict(pc=None,bytes_hex=(bytes([0xf2,0x0f,0x59])+encoding).hex(),kind=1,dest=dest,source=0,address=address))
reference=R/'generated/sse/legacy-mulsd-reference';proof=json.loads((reference/'result.json').read_text());assert proof['pass'] and proof['vectors_sha256']==sha(reference/'vectors.json')
for path,digest in proof['source_sha256'].items():assert sha(R/path)==digest
assert proof['requests_sha256']==sha(reference/'requests.txt') and proof['results_sha256']==sha(reference/'reference.txt')
corpus=json.loads((reference/'vectors.json').read_text());assert len(corpus)==proof['numeric_vectors']
from f64_multiply_reference import multiply
samples=[]
for index in range(128):
 c,a0,b0,value,after=corpus[(index*811)%len(corpus)];same,raised=multiply(a0,a0,c);samples.append((c,a0,b0,value,after,same,c|raised))
def row(q):return '{'+str(q[0])+'u,'+','.join('UINT64_C('+str(x)+')' if i in [1,2,3,5] else str(x)+'u' for i,x in enumerate(q[1:],1))+'}'
(out/'cases.inc').write_text('typedef struct {uint32_t control;uint64_t a,b,value;uint32_t after;} sample;\nstatic const sample corpus[]={'+','.join(row(q) for q in corpus)+'};\ntypedef struct {uint32_t control;uint64_t a,b,value;uint32_t after;uint64_t same;uint32_t same_after;} short_sample;\nstatic const short_sample samples[]={'+','.join(row(q) for q in samples)+'};\n')
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('address16','67f20f5900'),('segment','64f20f5900'),('lock','f0f20f59c0')]:
 run=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append(dict(name=name,bytes_hex=raw,exit=run.returncode))
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned kind,dest,source,address;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(v[k]) for k in ['kind','dest','source','address'])+'}' for v in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/legacy_mulsd_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+(['-DTRIBESPAD_MULSD_HOST_FAULT_TESTS'] if not a.simulator_device else [])+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
protected=sum(v['kind'] for v in records);paths=[c,pathlib.Path(__file__).resolve(),R/'scripts/f64_multiply_reference.py',R/'port/core/sse_arithmetic.c',R/'port/core/sse_arithmetic.h']+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']]
report={'pass':True,'state':'finished','cases':len(corpus)+(len(records)-1)*128+protected*128,'protected_page_cases':protected*128,'host_unmasked_checks':0 if a.simulator_device else 5,'host_nan_source_faults':0 if a.simulator_device else 1,'corpus_cases':len(corpus),'numeric_reference':proof,'fixture_corpus_sha256':sha(out/'cases.inc'),'blocks':len(records),'records':records,'negative':negative,'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'Original MULSD absolute MEM64 full exact multiplication/status corpus, all REG aliases/four unaligned memory forms, complete440CPU destinationlow64/status publication and uppervector/flags preservation. SoftFloat TLS fourfield preservation, exact8byte guard reads, host isolated IE/DE/PE/OE/UE stop without destination publication and inaccessible source even with NaN destination. Physical Intel/device, Windows priority/delivery and fullDLL/gameplay remain unfinished.'}
assert run.stdout.strip()==f"PASS: {report['cases']} emitted legacy MULSD checks; {report['protected_page_cases']} protected8byte reads; {0 if a.simulator_device else 5} unmasked stops; TLS/full CPU preservation"
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
