#!/usr/bin/env python3
"""Execute emitted VEX dword extraction/insertion with full CPU expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/sse/dword-move-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;records=[]
audit=R/'generated/analysis/ifc22/all-vex-decode-gap.json'
for row in json.loads(audit.read_text())['missing']:
 if row['mnemonic'] not in ['vpextrd','vpinsrd']:continue
 pc=int(row['pc'],16);raw=bytes.fromhex(row['bytes_hex']);assert pe.get_data(pc-0x10000000,len(raw))==raw;ins=next(md.disasm(raw,pc));ops=ins.operands;op=int(ins.mnemonic=='vpinsrd');assert all(o.type==capstone.x86_const.X86_OP_REG for o in ops[:-1]);idx=lambda o:int(ins.reg_name(o.reg)[3:]) if ins.reg_name(o.reg).startswith('xmm') else ['eax','ecx','edx','ebx','esp','ebp','esi','edi'].index(ins.reg_name(o.reg))
 records.append(dict(pc=pc,bytes_hex=raw.hex(),op=op,kind=0,dest=idx(ops[0]),merge=idx(ops[1]) if op else 0,source=idx(ops[2] if op else ops[1]),address=0,control=ops[-1].imm))
assert len(records)==9
for w in range(2):
 for op in range(2):
  for dest in range(8):
   for merge in range(8 if op else 1):
    for control in range(4):
     prefix=bytes([0xc4,0xe3,(w<<7)|((15-merge)<<3)|1,0x22 if op else 0x16])
     for source in range(8):
      modrm=0xc0|dest<<3|source if op else 0xc0|source<<3|dest
      records.append(dict(pc=None,bytes_hex=(prefix+bytes([modrm,control])).hex(),op=op,kind=0,dest=dest,merge=merge,source=source,address=0,control=control))
     for address,encoding in enumerate([bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x10]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')]):
      records.append(dict(pc=None,bytes_hex=(prefix+encoding+bytes([control])).hex(),op=op,kind=1,dest=dest if op else 0,merge=merge,source=0 if op else dest,address=address,control=control))
for w in range(2):
 for op in range(2):
  for control in range(256):records.append(dict(pc=None,bytes_hex=(bytes([0xc4,0xe3,(w<<7)|((15-(2 if op else 0))<<3)|1,0x22 if op else 0x16,0xc3,control])).hex(),op=op,kind=0,dest=0 if op else 3,merge=2 if op else 0,source=3 if op else 0,address=0,control=control))
for v in records:
 raw=bytes.fromhex(v['bytes_hex']);ins=next(md.disasm(raw,0));v['capstone_raw_assembly']=ins.mnemonic+' '+ins.op_str
 # Capstone5 labels W1 as qword even in32-bit mode. Intel specifies WIG
 # here. Normalize only diagnostic disassembly; the tested bytes stay W1.
 if raw[2]&128:ins=next(md.disasm(raw[:2]+bytes([raw[2]&127])+raw[3:],0))
 assert ins.mnemonic==['vpextrd','vpinsrd'][v['op']];v['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('wide','c4e37d16c001'),('vvvv','c4e37116c001'),('pp','c4e37816c001'),('extended','c4c37916c001'),('segment','64c4e379160601'),('address16','67c4e379160001'),('legacy66','66c4e37916c001'),('insert-wide','c4e36d22c001')]:
 run=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append(dict(name=name,bytes_hex=raw,exit=run.returncode))
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned op,kind,dest,merge,source,address,control;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(v[k]) for k in ['op','kind','dest','merge','source','address','control'])+'}' for v in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/dword_move_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
protected=sum(v['kind'] for v in records);paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']]
report={'pass':True,'state':'finished','cases':(len(records)-9)*16+9*128+protected,'protected_page_cases':protected,'blocks':len(records),'records':records,'negative':negative,'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'9 original dword moves and finiteC4WIG/allroles/allimm/four memory forms. Full440CPU/allMXCSR and source/destination guards;128 one-bit vectors at originals, exact4byteprotected reads/writes at eachMEMbody. No canonicalW1IA32/hardwareIntel/Windowsfault/fullmodule/gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
