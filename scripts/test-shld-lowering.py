#!/usr/bin/env python3
"""Execute emitted SHLD32 original, immediate/count aliases and MEM32 forms."""
import argparse,hashlib,json,pathlib,re,subprocess
import pefile
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/shift/shld-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True)
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
ref=R/'generated/shift/shld-reference';proof=json.loads((ref/'result.json').read_text());assert proof['pass'] and proof['cases']==65536 and proof['binary_sha256']==sha(ref/'reference')
for p,d in proof['source_sha256'].items():assert sha(R/p)==d
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));pc=0x100206dc;raw=bytes.fromhex('0fa4c208');assert pe.get_data(pc-0x10000000,4)==raw
records=[dict(pc=pc,bytes_hex=raw.hex(),kind=0,dest=2,source=0,address=0,cl=0,count=8)]
def add(d,s,kind,adr,cl,count,encoding):records.append(dict(pc=None,bytes_hex=(bytes([15,165 if cl else 164])+encoding+(b'' if cl else bytes([count]))).hex(),kind=kind,dest=d,source=s,address=adr,cl=cl,count=count))
for count in range(256):add(2,0,0,0,0,count,bytes([0xc2]))
for d in range(8):
 for s in range(8):
  for cl in range(2):add(d,s,0,0,cl,8,bytes([0xc0|s<<3|d]))
for s in range(8):
 for adr,enc in enumerate([bytes([s<<3]),bytes([0x44|s<<3,0x24,0x10]),bytes([0x44|s<<3,0xb3,0xf0]),bytes([5|s<<3])+bytes.fromhex('10002000')]):
  for cl in range(2):add(0,s,1,adr,cl,8,enc)
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procs=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procs)==len(records)
negative=[]
for name,raw in [('segment','640fa40008'),('address16','670fa40008'),('lock','f00fa4c208'),('width16','660fa4c208')]:
 q=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert q.returncode!=0;negative.append(dict(name=name,exit=q.returncode,bytes_hex=raw))
support=R/'port/llasm-support';ll=out/'bodies.llasm';ll.write_text('include tribes_shift.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procs));ir=out/'bodies.ll';subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(ll)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned kind,dest,source,address,cl,count;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(v[k]) for k in ['kind','dest','source','address','cl','count'])+'}' for v in records)+'};\n')
sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();c=R/'tests/shld_lowering_test.c';exe=out/'native';subprocess.run(['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out),str(ir),str(c),str(support/'tribes_shift.c'),str(R/'port/core/shift.c'),'-o',str(exe)],check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);expected=f'PASS: {65536+(len(records)-1)*256+64*256} emitted SHLD full CPU checks; {64*256} protected4byte RMW checks';assert run.returncode==0 and run.stdout.strip()==expected and not run.stderr,(run.returncode,run.stdout,run.stderr)
(out/'run.log').write_text(run.stdout);paths=[c,pathlib.Path(__file__).resolve(),R/'scripts/sse_instruction_fixture.py',R/'port/core/shift.c',R/'port/core/shift.h',support/'tribes_shift.c',support/'tribes_shift.llinc',support/'llasm_cpu.h'];report={'pass':True,'cases':65536+(len(records)-1)*256+64*256,'protected_page_cases':64*256,'blocks':len(records),'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'canonical_reference':proof,'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'binary_sha256':sha(exe),'ir_sha256':sha(ir),'output':run.stdout.strip(),'records':records,'negative':negative,'scope':'Full-flags finite SHLD32 original/all256 immediate counts/all64 register aliases with CL or imm8/all32 memory forms CL or imm8. Original every16bit initial flag combination; masked counts/CL destination/source alias/full440CPU and exact4byte RMW boundary. Undefined AF/multiOF deliberate preserve policy. Not optimized lowering,16bit,Windows faults,physicaldevice/fullDLL/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
