#!/usr/bin/env python3
"""Execute emitted VINSERTI128 ARM bodies with independent byte/state expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/inserti-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));records=[];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
prior=R/'generated/sse/blendvb-checkpoint-20261001/remaining-vex-decode-gap.json'
for row in json.loads(prior.read_text())['missing']:
 if row['mnemonic']!='vinserti128':continue
 ins=next(decoder.disasm(bytes.fromhex(row['bytes_hex']),int(row['pc'],16)));ops=ins.operands;index=lambda op:int(ins.reg_name(op.reg)[3:]);assert ops[2].type==capstone.x86_const.X86_OP_REG
 assert pe.get_data(int(row['pc'],16)-pe.OPTIONAL_HEADER.ImageBase,row['size']).hex()==row['bytes_hex']
 records.append({'pc':int(row['pc'],16),'bytes_hex':row['bytes_hex'],'kind':0,'dest':index(ops[0]),'merge':index(ops[1]),'source':index(ops[2]),'address':0,'disp':0,'control':ops[3].imm})
assert len(records)==3
def add(dest,merge,source,control,address=None):
 enc=bytes([0xc0|dest<<3|source]) if address is None else [bytes([dest<<3]),bytes([0x44|dest<<3,0x24,16]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')][address]
 records.append({'pc':None,'bytes_hex':(bytes([0xc4,0xe3,((15-merge)<<3)|5,0x38])+enc+bytes([control])).hex(),'kind':int(address is not None),'dest':dest,'merge':merge,'source':source,'address':address or 0,'disp':16 if address==1 else 0,'control':control})
for dest in range(8):
 for merge in range(8):
  for control in [0,1,2,3,254,255]:
   for source in range(8):add(dest,merge,source,control)
   for address in range(4):add(dest,merge,0,control,address)
for control in range(256):
 add(0,1,2,control)
 for address in range(4):add(0,1,0,control,address)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for r in records:
 ins=next(decoder.disasm(bytes.fromhex(r['bytes_hex']),0));assert ins.mnemonic=='vinserti128' and ins.size==len(bytes.fromhex(r['bytes_hex']));r['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('W1','c4e3fd38c001'),('L0','c4e37938c001'),('pp0','c4e37c38c001'),('fs','64c4e37d380601'),('address16','67c4e37d380001'),('lock','f0c4e37d38c001'),('legacy66','66c4e37d38c001'),('map','c4e27d380601'),('extended','c4c37d380601'),('vvvvextended','c4e33d38c001')]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append({'name':name,'bytes_hex':raw,'exit':run.returncode})
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned kind,dest,merge,source,address,disp,control;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(r[k]) for k in ['kind','dest','merge','source','address','disp','control'])+'}' for r in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/inserti_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']];report={'pass':True,'state':'finished','cases':len(records)*128,'blocks':len(records),'records':records,'negative':negative,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'Actual original VINSERTI128 bytes; all3-role aliases, low immediate bit selects half, all256 immediates at canonicalREG/4MEMforms, exact16byte input/fullCPU/flags/allMXCSR/guardedmemory. No physicalIntel/fullDLLinit/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
