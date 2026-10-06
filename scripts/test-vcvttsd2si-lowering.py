#!/usr/bin/env python3
"""Execute actual VCVTTSD2SI bodies with independent integer/state expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/vcvttsd2si-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));raw='c5fb2c442440';assert pe.get_data(0xe431,6).hex()==raw
records=[{'pc':0x1000e431,'bytes_hex':raw,'kind':1,'dest':0,'source':0,'address':1}]
def prefix(form):
 return bytes([0xc5,0xfb]) if form==0 else bytes([0xc4,0xe1,0xfb if form==2 else 0x7b])
for form in range(3):
 for dest in range(8):
  for source in range(8):records.append({'pc':None,'bytes_hex':(prefix(form)+bytes([0x2c,0xc0|dest<<3|source])).hex(),'kind':0,'dest':dest,'source':source,'address':0})
  for address,encoding in enumerate([bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x40]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')]):records.append({'pc':None,'bytes_hex':(prefix(form)+bytes([0x2c])+encoding).hex(),'kind':1,'dest':dest,'source':0,'address':address})
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for r in records:
 ins=next(decoder.disasm(bytes.fromhex(r['bytes_hex']),0));assert ins.mnemonic=='vcvttsd2si' and ins.size==len(bytes.fromhex(r['bytes_hex']));r['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('reserved-reg','c5f32cc0'),('reserved-mem','c5f32c06'),('length','c5ff2c06'),('pp66','c5f92c06'),('ppf3','c5fa2c06'),('fs','64c5fb2c06'),('address16','67c5fb2c00'),('lock','f0c5fb2c06'),('legacy66','66c5fb2c06'),('map','c4e37b2c06'),('extended','c4c17b2c06')]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append({'name':name,'bytes_hex':raw,'exit':run.returncode})
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned kind,dest,source,address;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(r[k]) for k in ['kind','dest','source','address'])+'}' for r in records)+'};\n')
reference=R/'generated/sse/vcvttsd2si-reference';ref_report=json.loads((reference/'result.json').read_text());assert ref_report['pass'] and ref_report['state']=='finished'
for name,key in [('cases.inc','corpus_sha256'),('requests.txt','request_sha256'),('reference.txt','reference_sha256')]:assert sha(reference/name)==ref_report[key]
for name,h in ref_report['source_sha256'].items():assert sha(R/name)==h
(out/'cases.inc').write_bytes((reference/'cases.inc').read_bytes())
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/vcvttsd2si_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']];report={'pass':True,'state':'finished','cases':len(records)*2048,'blocks':len(records),'records':records,'negative':negative,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'reference':ref_report,'scope':'Actual original/synthetic emitted C5/C4 IA-32 WIG L0 VCVTTSD2SI all GPR/XMM roles, exact integer-division expectations also checked against canonical W0 REG/MEM Rosetta; full CPU/vector/flags/memory and TLS preservation. W1 follows non64 SDM contract; no precise Windows faults, full DLL initialization or gameplay claim.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
