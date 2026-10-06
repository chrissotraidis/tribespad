#!/usr/bin/env python3
"""Execute emitted legacy BTS with full CPU and memory expectations."""
import argparse,hashlib,json,pathlib,re,subprocess,os
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/sse/bts-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
ref=R/'generated/sse/bts-reference';proof=json.loads((ref/'result.json').read_text());assert proof['pass'] and proof['cases']==196608 and sha(ref/'reference')==proof['binary_sha256']
for path,digest in proof['source_sha256'].items():assert sha(R/path)==digest
assert (ref/'run.log').read_text().strip()==proof['output']
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;records=[]
pc=0x100062fe;raw=bytes.fromhex('0fabd0');assert pe.get_data(pc-0x10000000,len(raw))==raw
records.append(dict(pc=pc,bytes_hex=raw.hex(),kind=0,dest=0,source=2,count=0))
for dest in range(8):
 for src in range(8):records.append(dict(pc=None,bytes_hex=bytes([0x0f,0xab,0xc0|src<<3|dest]).hex(),kind=0,dest=dest,source=src,count=0))
 for count in range(256):records.append(dict(pc=None,bytes_hex=bytes([0x0f,0xba,0xe8|dest,count]).hex(),kind=1,dest=dest,source=0,count=count))
for dest in range(8):
 for src in range(8):records.append(dict(pc=None,bytes_hex=(bytes.fromhex('39d9')+bytes([0x0f,0xab,0xc0|src<<3|dest])+bytes.fromhex('0f94c30f92c2')).hex(),kind=2,dest=dest,source=src,count=0))
for raw in ['640fabd0','650fabd0']:records.append(dict(pc=None,bytes_hex=raw,kind=0,dest=0,source=2,count=0))
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('address16','670fabd0'),('lock','f00fabd0'),('word','660fabd0'),('memory','0fab10')]:
 run=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append(dict(name=name,bytes_hex=raw,exit=run.returncode))
optimized=[dict(v,kind=3) for v in records if v['kind']==2];opt=out/'fixture-optimized';run=fixture(opt,optimized,original,srw);assert not run.returncode,(run.stdout,run.stderr)
env={k:v for k,v in os.environ.items() if k not in ['TRIBES_SRW_FULL_FLAGS','HALOPAD_SRW_DIAG']};run=subprocess.run([str(srw),'fixture.exe','fixture.llasm'],cwd=opt,env=env,capture_output=True,text=True,timeout=60);(opt/'srw-optimized.log').write_text(run.stdout+run.stderr);assert not run.returncode,(run.stdout,run.stderr)
opttext=(opt/'seg01_code.llinc').read_text();optprocedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',opttext,re.M|re.S));assert len(optprocedures)==64
first_optimized=len(records);records.extend(optimized)
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('define CF 1\ndefine PF 4\ndefine AF 16\ndefine ZF 64\ndefine SF 128\ndefine OF 2048\ndefine CF_SHIFT 0\ndefine PF_SHIFT 2\ndefine AF_SHIFT 4\ndefine OF_SHIFT 11\ndefine SF_SHIFT 7\ndefine ZF_SHIFT 6\ninclude tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures)+''.join('proc move_'+str(first_optimized+i)+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for i,m in enumerate(optprocedures)));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned kind,dest,source,count;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(v[k]) for k in ['kind','dest','source','count'])+'}' for v in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/bts_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']]
report={'pass':True,'state':'finished','cases':65536+(len(records)-1)*256,'full_flags_cases':623104,'optimized_context_cases':16384,'optimized_contexts':64,'blocks':len(records),'records':records,'negative':negative,'canonical_reference':{'checks':proof['cases'],'report_sha256':sha(ref/'result.json'),'binary_sha256':proof['binary_sha256']},'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'Original BTS EAX,EDX, all64 REG32 aliases, all8destinations/all256IMM8 values,64 full-flags and64 optimized CMP/BTS/SETZ/SETC contexts and two ignored segment examples. Full440CPU checks with selected-bit CF, preserved ZF and deterministic retain policy for undefined flags in full-flags bodies; undefined OF/SF/AF/PF excluded only in optimized context comparisons. Original full index/bitbasis/deterministic corpus; no physicalIntel/memoryBTS/fullDLL/gameplay proof.'}
assert run.stdout.strip()==f"PASS: {report['cases']} emitted BTS checks; REG/IMM aliases, preserved ZF and full CPU policy"
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
