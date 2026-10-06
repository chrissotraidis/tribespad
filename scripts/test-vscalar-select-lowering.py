#!/usr/bin/env python3
"""Execute actual scalar MIN/MAX/COMISD bodies with independent integer/state expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/sse/vscalar-select-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));records=[]
prior=R/'generated/sse/vcvttsd2si-checkpoint-20261001/remaining-vex-decode-gap.json'
for row in json.loads(prior.read_text())['missing']:
 if row['mnemonic'] not in ['vminss','vmaxss','vcomisd']:continue
 op=['vminss','vmaxss','vcomisd'].index(row['mnemonic']);words=row['assembly'].split(' ',1)[1].split(', ')
 dest=int(words[0][3:]);memory='[' in words[-1];merge=int(words[1][3:]) if op!=2 else 0;source=0 if memory else int(words[-1][3:])
 assert not memory or words[-1]=='dword ptr [ebp + 0x10]'
 assert pe.get_data(int(row['pc'],16)-pe.OPTIONAL_HEADER.ImageBase,row['size']).hex()==row['bytes_hex']
 records.append({'pc':int(row['pc'],16),'bytes_hex':row['bytes_hex'],'op':op,'kind':int(memory),'dest':dest,'source':source,'merge':merge,'address':4 if memory else 0})
assert len(records)==16
def prefix(form,op,merge):
 v=0x79 if op==2 else ((15-merge)<<3)|2
 return bytes([0xc5,0x80|v]) if form==0 else bytes([0xc4,0xe1,v|(0x80 if form==2 else 0)])
for form in range(3):
 for op in range(3):
  for dest in range(8):
   for merge in range(1 if op==2 else 8):
    opcode=0x2f if op==2 else 0x5f if op else 0x5d
    for source in range(8):records.append({'pc':None,'bytes_hex':(prefix(form,op,merge)+bytes([opcode,0xc0|dest<<3|source])).hex(),'op':op,'kind':0,'dest':dest,'source':source,'merge':merge,'address':0})
    for address,encoding in enumerate([bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x10]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')]):records.append({'pc':None,'bytes_hex':(prefix(form,op,merge)+bytes([opcode])+encoding).hex(),'op':op,'kind':1,'dest':dest,'source':0,'merge':merge,'address':address})
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for r in records:
 ins=next(decoder.disasm(bytes.fromhex(r['bytes_hex']),0));assert ins.mnemonic==['vminss','vmaxss','vcomisd'][r['op']] and ins.size==len(bytes.fromhex(r['bytes_hex']));r['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr);text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('min-length','c5fe5dc0'),('max-length','c5fe5fc0'),('compare-length','c5fd2fc0'),('reserved-compare','c5f12fc0'),('min-pp66','c5f95dc0'),('max-ppf2','c5fb5fc0'),('fs','64c5fa5d06'),('address16','67c5fa5f00'),('lock','f0c5f92fc0'),('legacy66','66c5fa5fc0'),('map','c4e37a5d06'),('extended','c4c17a5f06')]:
 run=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append({'name':name,'bytes_hex':raw,'exit':run.returncode})
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned op,kind,dest,source,merge,address;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(r[k]) for k in ['op','kind','dest','source','merge','address'])+'}' for r in records)+'};\n')
reference=R/'generated/sse/vscalar-select-reference';ref_report=json.loads((reference/'result.json').read_text());assert ref_report['pass'] and ref_report['state']=='finished'
for name,key in [('cases.inc','corpus_sha256'),('requests.txt','requests_sha256'),('reference.txt','reference_sha256')]:assert sha(reference/name)==ref_report[key]
for name,h in ref_report['sources_sha256'].items():assert sha(R/name)==h
(out/'cases.inc').write_bytes((reference/'cases.inc').read_bytes())
coverage={}
for op in range(3):
 count=ref_report['per_operation'][op];seen={((block*512+i)*19)%count for block,r in enumerate(records) if r['op']==op for i in range(512)};assert len(seen)==count;coverage[str(op)]={'covered':len(seen),'candidates':count}

platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/vscalar_select_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True);run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']];report={'pass':True,'state':'finished','cases':len(records)*512,'blocks':len(records),'records':records,'negative':negative,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'reference':ref_report,'candidate_coverage':coverage,'scope':'Actual16 original sites and synthetic C5/C4 WIG L0 scalar MINSS/MAXSS/COMISD; all vector roles/aliases and scalar memory addressing; exact rational candidates also checked against canonical REG/MEM/diagonal Rosetta; full CPU/vector/flags/memory/TLS. No physical Intel, precise Windows fault delivery, full DLL initialization or gameplay claim.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
