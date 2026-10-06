#!/usr/bin/env python3
"""Execute emitted legacy XMM word extraction with full CPU expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/sse/word-extract-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;records=[]
pc=0x1010c60f;raw=bytes.fromhex('660fc5c003');assert pe.get_data(pc-0x10000000,len(raw))==raw
records.append(dict(pc=pc,bytes_hex=raw.hex(),kind=0,dest=0,source=0,address=0,control=3))
for form in range(3):
 for src in range(8):
  for control in range(8):
   if form<2:
    for dest in range(8):
     raw=(bytes([0x66,0x0f,0xc5,0xc0|dest<<3|src,control]) if form==0 else bytes([0x66,0x0f,0x3a,0x15,0xc0|src<<3|dest,control]))
     records.append(dict(pc=None,bytes_hex=raw.hex(),kind=0,dest=dest,source=src,address=0,control=control))
   else:
    for address,encoding in enumerate([bytes([src<<3]),bytes([0x44|src<<3,0x24,0x10]),bytes([0x44|src<<3,0xb3,0xef]),bytes([5|src<<3])+bytes.fromhex('03002000')]):
     records.append(dict(pc=None,bytes_hex=(bytes([0x66,0x0f,0x3a,0x15])+encoding+bytes([control])).hex(),kind=1,dest=0,source=src,address=address,control=control))
for form in range(3):
 for control in range(256):
  raw=bytes([0x66,0x0f,0xc5,0xc3,control]) if form==0 else bytes([0x66,0x0f,0x3a,0x15,0xc3 if form==1 else 0,control])
  records.append(dict(pc=None,bytes_hex=raw.hex(),kind=int(form==2),dest=0 if form==0 else 3,source=3 if form==0 else 0,address=0,control=control))
for v in records:
 ins=next(md.disasm(bytes.fromhex(v['bytes_hex']),0));assert ins.mnemonic=='pextrw';v['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('mmx','0fc5c003'),('segment','64660f3a150603'),('address16','67660f3a150003'),('lock','f0660fc5c003')]:
 run=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append(dict(name=name,bytes_hex=raw,exit=run.returncode))
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned kind,dest,source,address,control;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(v[k]) for k in ['kind','dest','source','address','control'])+'}' for v in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/word_extract_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
protected=sum(v['kind'] for v in records);paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']]
report={'pass':True,'state':'finished','cases':(len(records)-1)*16+128+protected,'protected_page_cases':protected,'blocks':len(records),'records':records,'negative':negative,'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'Original legacy extraction and both XMM REG encodings/MEM16, all roles/imm8, four memory forms, full440CPU and exact2byte guarded stores. Legacy vectors preserved. Not MMX lowering, canonical fullDLL, Windows fault or gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
