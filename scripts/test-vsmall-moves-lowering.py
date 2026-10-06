#!/usr/bin/env python3
"""Execute actual VMOVD/VMOVQ bodies with independent integer/state expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/vsmall-moves-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));records=[];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
prior=R/'generated/sse/vinteger-logic-checkpoint-20261001/remaining-vex-decode-gap.json'
for row in json.loads(prior.read_text())['missing']:
 if row['mnemonic'] not in ['vmovd','vmovq']:continue
 ins=next(decoder.disasm(bytes.fromhex(row['bytes_hex']),int(row['pc'],16)));ops=ins.operands;quad=int(row['mnemonic']=='vmovq');memory=next((i for i,o in enumerate(ops) if o.type==capstone.x86_const.X86_OP_MEM),None)
 index=lambda o:int(ins.reg_name(o.reg)[3:]) if ins.reg_name(o.reg).startswith('xmm') else ['eax','ecx','edx','ebx','esp','ebp','esi','edi'].index(ins.reg_name(o.reg))
 if memory is not None:
  m=ops[memory].mem;assert not m.index and ins.reg_name(m.base) in ['esp','ebp'];address=1 if ins.reg_name(m.base)=='esp' else 4;disp=m.disp&0xffffffff;kind=1 if memory==1 else 2;dest=index(ops[0]) if kind==1 else 0;source=0 if kind==1 else index(ops[1])
 else:
  kind=0 if quad else 3 if ins.reg_name(ops[0].reg).startswith('xmm') else 4;dest=index(ops[0]);source=index(ops[1]);address=0;disp=0
 assert pe.get_data(int(row['pc'],16)-pe.OPTIONAL_HEADER.ImageBase,row['size']).hex()==row['bytes_hex']
 records.append({'pc':int(row['pc'],16),'bytes_hex':row['bytes_hex'],'quad':quad,'kind':kind,'dest':dest,'source':source,'address':address,'disp':disp})
assert len(records)==77
def prefix(form,pp):return bytes([0xc5,0xf8|pp]) if form==0 else bytes([0xc4,0xe1,0x78|pp|(0x80 if form==2 else 0)])
for form in range(3):
 for quad in range(2):
  for store in range(2):
   opcode=(0xd6 if store else 0x7e) if quad else (0x7e if store else 0x6e);pp=2 if quad and not store else 1
   for dest in range(8):
    for source in range(8):
     modrm=0xc0|((source if store else dest)<<3)|(dest if store else source);records.append({'pc':None,'bytes_hex':(prefix(form,pp)+bytes([opcode,modrm])).hex(),'quad':quad,'kind':0 if quad else 4 if store else 3,'dest':dest,'source':source,'address':0,'disp':0})
    for address,encoding in enumerate([bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x10]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')]):records.append({'pc':None,'bytes_hex':(prefix(form,pp)+bytes([opcode])+encoding).hex(),'quad':quad,'kind':2 if store else 1,'dest':dest,'source':dest,'address':address,'disp':16 if address==1 else 0})
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for r in records:
 ins=next(decoder.disasm(bytes.fromhex(r['bytes_hex']),0));assert ins.mnemonic==('vmovq' if r['quad'] else 'vmovd') and ins.size==len(bytes.fromhex(r['bytes_hex']));r['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('d-vvvv','c5f16ec0'),('q-vvvv','c5f27ec0'),('d-length','c5fd7ec0'),('q-length','c5fdd606'),('d-pp0','c5f86ec0'),('q-ppf2','c5fbd606'),('fs','64c5f9d606'),('address16','67c5fa7e00'),('lock','f0c5f96ec0'),('legacy66','66c5f9d6c0'),('map','c4e3796e06'),('extended','c4c17a7e06')]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append({'name':name,'bytes_hex':raw,'exit':run.returncode})
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned quad,kind,dest,source,address,disp;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case cases[]={'+','.join('{'+','.join(str(r[k]) for k in ['quad','kind','dest','source','address','disp'])+'}' for r in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/vsmall_moves_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']];report={'pass':True,'state':'finished','cases':len(records)*512,'blocks':len(records),'records':records,'negative':negative,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'All77 original VMOVD/VMOVQ sites and synthetic C5/C4 IA32 WIG L0, both register directions/encodings and exact4/8-byte unaligned memory. Full CPU/vector/flags/MXCSR/guards, no physical Intel/full DLL initialization/gameplay claim.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
