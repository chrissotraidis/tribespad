#!/usr/bin/env python3
"""Execute emitted IA32 BMI2 shifts with independent full CPU expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/bmishift-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));records=[];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
prior=R/'generated/analysis/ifc22/all-vex-decode-gap.json'
for row in json.loads(prior.read_text())['records']:
 if row['mnemonic'] not in ['shlx','shrx']:continue
 ins=next(decoder.disasm(bytes.fromhex(row['bytes_hex']),int(row['pc'],16)));ops=ins.operands;gprs=['eax','ecx','edx','ebx','esp','ebp','esi','edi'];index=lambda op:gprs.index(ins.reg_name(op.reg));memory=ops[1].type==capstone.x86_const.X86_OP_MEM
 assert pe.get_data(int(row['pc'],16)-pe.OPTIONAL_HEADER.ImageBase,row['size']).hex()==row['bytes_hex']
 if memory:assert ins.reg_name(ops[1].mem.base)=='esp' and ops[1].mem.index==0
 records.append({'pc':int(row['pc'],16),'bytes_hex':row['bytes_hex'],'op':int(ins.mnemonic=='shrx'),'kind':int(memory),'dest':index(ops[0]),'source':0 if memory else index(ops[1]),'countreg':index(ops[2]),'address':1 if memory else 0,'disp':ops[1].mem.disp if memory else 0})
assert len(records)==13
def add(op,w,dest,source,countreg,address=None):
 enc=bytes([0xc0|dest<<3|source]) if address is None else [bytes([dest<<3]),bytes([0x44|dest<<3,0x24,16]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')][address]
 records.append({'pc':None,'bytes_hex':(bytes([0xc4,0xe2,(w<<7)|((15-countreg)<<3)|(3 if op else 1),0xf7])+enc).hex(),'op':op,'kind':int(address is not None),'dest':dest,'source':source,'countreg':countreg,'address':address or 0,'disp':16 if address==1 else 0})
for op in range(2):
 for w in range(2):
  for dest in range(8):
   for countreg in range(8):
    for source in range(8):add(op,w,dest,source,countreg)
    for address in range(4):add(op,w,dest,0,countreg,address)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for r in records:
 ins=next(decoder.disasm(bytes.fromhex(r['bytes_hex']),0));assert ins.mnemonic in ['shlx','shrx'] and ins.size==len(bytes.fromhex(r['bytes_hex']));r['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('L1','c4e27df7c0'),('pp0','c4e278f7c0'),('ppF3','c4e27af7c0'),('fs','64c4e279f706'),('address16','67c4e279f700'),('lock','f0c4e279f7c0'),('legacy66','66c4e279f7c0'),('map','c4e379f706'),('extended','c4c279f706'),('vvvvextended','c4e239f7c0'),('C5','c5f9f7c0')]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append({'name':name,'bytes_hex':raw,'exit':run.returncode})
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_shift.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned op,kind,dest,source,countreg,address,disp;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(r[k]) for k in ['op','kind','dest','source','countreg','address','disp'])+'}' for r in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();c=R/'tests/bmishift_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+[str(ir),str(c),str(support/'tribes_shift.c'),str(R/'port/core/shift.c')]+['-o',str(exe)];subprocess.run(cmd,check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve(),R/'port/core/shift.c',R/'port/core/shift.h']+[support/n for n in ['tribes_shift.c','tribes_shift.llinc','llasm_cpu.h']];report={'pass':True,'state':'finished','cases':len(records)*256,'blocks':len(records),'records':records,'negative':negative,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'output':run.stdout.strip(),'scope':'Actual 13 SHLX/SHRX sites; allGPRaliases/IA32WIG/countmask/exactdwordMEM/fullCPU/flags/allMXCSR/guardedmemory. No physicalIntel/fullDLLinit/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
