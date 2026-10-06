#!/usr/bin/env python3
"""Execute strict original packed shifts with independent integer expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();out=R/'generated/sse/vpacked-shifts-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));records=[];names=['vpslld','vpsrlq','vpslldq','vpsrldq','vpsllvd'];decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
for row in json.loads((R/'generated/sse/vmaddadd-checkpoint-20261001/remaining-vex-decode-gap.json').read_text())['missing']:
 if row['mnemonic'] not in names:continue
 ins=next(decoder.disasm(bytes.fromhex(row['bytes_hex']),int(row['pc'],16)));ops=ins.operands;index=lambda o:int(ins.reg_name(o.reg)[3:]);kind=0 if ops[2].type==capstone.x86_const.X86_OP_IMM else 1;assert kind==0 or ops[2].type==capstone.x86_const.X86_OP_REG
 assert pe.get_data(int(row['pc'],16)-pe.OPTIONAL_HEADER.ImageBase,row['size']).hex()==row['bytes_hex'];records.append({'pc':int(row['pc'],16),'bytes_hex':row['bytes_hex'],'op':names.index(row['mnemonic']),'wide':int(ins.reg_name(ops[0].reg).startswith('ymm')),'kind':kind,'dest':index(ops[0]),'source':index(ops[1]),'countreg':index(ops[2]) if kind else 0,'count':ops[2].imm if not kind else 0,'address':0})
assert len(records)==22
def prefix(form,wide,value,op):
 v=((15-value)<<3)|1|(wide<<2);return bytes([0xc5,0x80|v]) if form==0 else bytes([0xc4,0xe2 if op==4 else 0xe1,v|(0x80 if form==2 else 0)])
def immediate(form,wide,op,dest,source,count):
 records.append({'pc':None,'bytes_hex':(prefix(form,wide,dest,op)+bytes([0x72 if op==0 else 0x73,0xc0|[6,2,7,3][op]<<3|source,count])).hex(),'op':op,'wide':wide,'kind':0,'dest':dest,'source':source,'countreg':0,'count':count,'address':0})
for form in range(3):
 for wide in range(2):
  for op in range(4):
   for dest in range(8):
    for source in range(8):
     for count in [0,1,15,16,31,32,63,64,255]:immediate(form,wide,op,dest,source,count)
   for count in range(256):immediate(form,wide,op,0,1,count)
  for op in [0,1,4]:
   if op==4 and form!=1:continue
   opcode=0x47 if op==4 else 0xf2 if op==0 else 0xd3
   for dest in range(8):
    for source in range(8):
     for countreg in range(8):records.append({'pc':None,'bytes_hex':(prefix(form,wide,source,op)+bytes([opcode,0xc0|dest<<3|countreg])).hex(),'op':op,'wide':wide,'kind':1,'dest':dest,'source':source,'countreg':countreg,'count':0,'address':0})
     for address,encoding in enumerate([bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x10]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')]):records.append({'pc':None,'bytes_hex':(prefix(form,wide,source,op)+bytes([opcode])+encoding).hex(),'op':op,'wide':wide,'kind':2,'dest':dest,'source':source,'countreg':0,'count':0,'address':address})
for r in records:
 ins=next(decoder.disasm(bytes.fromhex(r['bytes_hex']),0));assert ins.mnemonic==names[r['op']] and ins.size==len(bytes.fromhex(r['bytes_hex']));r['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('group','c5f972c005'),('immediate-memory','c5f9723005'),('pp0','c5f873d008'),('fs','64c5f9d306'),('address16','67c5f9d300'),('lock','f0c5f972f005'),('legacy66','66c5f9d3c0'),('variable-W1','c4e2f947c0'),('map','c4e379d306'),('extended','c4c179d306')]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append({'name':name,'bytes_hex':raw,'exit':run.returncode})
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc shift_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'cases.inc').write_text('typedef struct {unsigned op,wide,kind,dest,source,countreg,count,address;} shift_case;\n'+''.join('extern void shift_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('shift_'+str(i) for i in range(len(records)))+'};\nstatic const shift_case cases[]={'+','.join('{'+','.join(str(r[k]) for k in ['op','wide','kind','dest','source','countreg','count','address'])+'}' for r in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/vpacked_shifts_lowering_test.c';exe=out/'native';subprocess.run(['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)],check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[pathlib.Path(__file__).resolve(),c]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']];report={'pass':True,'state':'finished','cases':len(records)*128,'blocks':len(records),'records':records,'negative':negative,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'All22 original packed shift sites, every immediate value on canonical roles plus boundary counts at all roles, scalar64-bit count versus per-dword unsigned count, lane-local byte shifts, aliases/widths/fullCPU/flags/allMXCSR/guarded exact memory. No physicalIntel/fullDLL initialization/gameplay proof.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
