#!/usr/bin/env python3
"""Execute strict broadcast bodies against independent integer bit-copy expectations."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--preview',action='store_true');ap.add_argument('--simulator-device');a=ap.parse_args()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
compiler=R/'generated/tools'/('SRW-broadcast-preview' if a.preview else 'SRW-tribes');support=R/'generated/broadcast-preview/port/llasm-support' if a.preview else R/'port/llasm-support';out=R/'generated/sse'/('vbroadcast-preview' if a.preview else 'vbroadcast-lowering')/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));raw='c4e2791844240c';assert pe.get_data(0xcd26,7).hex()==raw
records=[{'pc':0x1000cd26,'bytes_hex':raw,'dest':0,'kind':0,'wide':0,'source':0,'memory':1,'address':1}]
for kind,wide in [(0,0),(0,1),(1,1),(2,1)]:
 for dest in range(8):
  prefix=bytes([0xc4,0xe2,0x79|wide*4,0x18+kind])
  if kind!=2:
   for source in range(8):records.append({'pc':None,'bytes_hex':(prefix+bytes([0xc0|dest<<3|source])).hex(),'dest':dest,'kind':kind,'wide':wide,'source':source,'memory':0,'address':0})
  for address,encoding in enumerate([bytes([dest<<3]),bytes([0x44|dest<<3,0x24,0x0c]),bytes([0x44|dest<<3,0xb3,0xef]),bytes([5|dest<<3])+bytes.fromhex('03002000')]):records.append({'pc':None,'bytes_hex':(prefix+encoding).hex(),'dest':dest,'kind':kind,'wide':wide,'source':0,'memory':1,'address':address})
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
for rec in records:
 ins=next(decoder.disasm(bytes.fromhex(rec['bytes_hex']),0));assert ins.mnemonic==['vbroadcastss','vbroadcastsd','vbroadcastf128'][rec['kind']];rec['assembly']=ins.mnemonic+' '+ins.op_str
srw=compiler/'SRW.exe';run=fixture(out/'fixture',records,original,srw);assert not run.returncode,(run.stdout,run.stderr)
negative=[]
for name,raw in [('fs','64c4e2791806'),('address16','67c4e2791800'),('vvvv','c4e2711806'),('w1','c4e2f91806'),('sd128','c4e2791906'),('f128register','c4e27d1ac2'),('extended','c4c2791806'),('lock','f0c4e2791806'),('legacy66','66c4e2791806'),('map0f','c4e1791806')]:
 rejected=fixture(out/('negative-'+name),[{'bytes_hex':raw}],original,srw);assert rejected.returncode!=0,(name,rejected.stdout,rejected.stderr);negative.append({'name':name,'bytes_hex':raw,'exit':rejected.returncode})
text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
llasm=R/'generated/tools/llasm/llasm';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc broadcast_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'broadcast_bodies.inc').write_text('typedef struct {unsigned dest,kind,wide,source,memory,address;} broadcast_case;\n'+''.join('extern void broadcast_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('broadcast_'+str(i) for i in range(len(records)))+'};\nstatic const broadcast_case cases[]={'+','.join('{'+','.join(str(r[k]) for k in ['dest','kind','wide','source','memory','address'])+'}' for r in records)+'};\n')
platform='simulator' if a.simulator_device else 'macos';sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();math=sse_inputs(platform);c=R/'tests/vbroadcast_lowering_test.c';exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+math['compile_flags']+[str(ir),str(c),str(support/'tribes_sse.c')]+math['sources']+math['libraries']+['-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve()]+[support/n for n in ['tribes_sse.c','tribes_sse.h','tribes_sse.llinc','llasm_cpu.h']];report={'pass':True,'state':'finished','preview':a.preview,'cases':len(records)*2048,'blocks':len(records),'records':records,'negative':negative,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'sse_math':math['evidence'],'output':run.stdout.strip(),'scope':'Strict emitted original/synthetic C4 W0 scalar/double/128-bit broadcast forms, independent integer bit-copy expectations and full CPU/guard checks. Register SS/SD are AVX2 forms. No direct x86 oracle, complete DLL initialization, precise Windows faults or gameplay claim.'};(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
