#!/usr/bin/env python3
"""Execute emitted legacy PSRLDQ with full CPU and memory expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/sse/legacy-shiftrbytes-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
ref=R/'generated/sse/legacy-shiftrbytes-reference';proof=json.loads((ref/'result.json').read_text());assert proof['pass'] and proof['cases']==65536 and sha(ref/'reference')==proof['binary_sha256']
for path,digest in proof['source_sha256'].items():assert sha(R/path)==digest
assert (ref/'run.log').read_text().strip()==proof['output']
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;records=[]
for pc,raw,count in [(0x10005fe9,bytes.fromhex('660f73d804'),4),(0x10005ff4,bytes.fromhex('660f73d808'),8),(0x10006003,bytes.fromhex('660f73d804'),4)]:
 assert pe.get_data(pc-0x10000000,len(raw))==raw
 records.append(dict(pc=pc,bytes_hex=raw.hex(),dest=0,count=count))
for dest in range(8):
 for count in range(256):records.append(dict(pc=None,bytes_hex=bytes([0x66,0x0f,0x73,0xd8|dest,count]).hex(),dest=dest,count=count))
for raw in ['64660f73d804','65660f73d804']:records.append(dict(pc=None,bytes_hex=raw,dest=0,count=4))
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('address16','67660f73d804'),('lock','f0660f73d804'),('memory','660f731804')]:
 run=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append(dict(name=name,bytes_hex=raw,exit=run.returncode))
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned dest,count;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(v[k]) for k in ['dest','count'])+'}' for v in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/legacy_shiftrbytes_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']]
report={'pass':True,'state':'finished','cases':3*65536+(len(records)-3)*256,'blocks':len(records),'records':records,'negative':negative,'canonical_reference':{'checks':proof['cases'],'report_sha256':sha(ref/'result.json'),'binary_sha256':proof['binary_sha256']},'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'Three original legacy PSRLDQ sites, all8 XMM destinations/all256 immediate counts and two ignored segment-prefix examples. Originals run all65536 guestMXCSR patterns with deterministic inputs; synthetic128 bitbasis and128 deterministic inputs per count. Full440CPU flags/MXCSR/YMM preservation. Canonical Rosetta source/binary/report/case table pinned; no physicalIntel/fullDLL/gameplay proof.'}
assert run.stdout.strip()==f"PASS: {report['cases']} emitted legacy PSRLDQ checks; all256 counts, full CPU preservation"
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
