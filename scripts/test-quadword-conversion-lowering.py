#!/usr/bin/env python3
"""Compile original/synthetic conversion instructions and execute emitted ARM."""
import argparse,hashlib,json,pathlib,re,subprocess
import capstone,pefile
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
out=R/'generated/sse/quadword-conversion-lowering'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inv=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inv['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads((srw.parent/'tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));audit_path=R/'generated/analysis/ifc22/quadword-conversion-contract/result.json';audit=json.loads(audit_path.read_text());assert audit['pass'] and audit['image_sha256']==sha(original)
records=[]
for op,row in enumerate(audit['records']):
 pc=int(row['pc'],16);assert pe.get_data(pc-0x10000000,6).hex()==row['bytes_hex'];records.append(dict(pc=pc,bytes_hex=row['bytes_hex'],op=op,kind=0,dest=0,source=0,address=0,disp=0))
prefixes=[bytes.fromhex(s) for s in ['62f17d0878','62f17d087a','62f1fd0878','62f1fd087a','62f1fe087a','62f1fe08e6','62f1fc085b']]
for op,prefix in enumerate(prefixes):
 scale=8 if op<2 else 16
 for dest in range(8):
  for source in range(8):records.append(dict(pc=None,bytes_hex=(prefix+bytes([0xc0|dest<<3|source])).hex(),op=op,kind=0,dest=dest,source=source,address=0,disp=0))
  forms=[(0,0,bytes([dest<<3])),(1,16*scale,bytes([0x44|dest<<3,0x24,0x10])),(2,-17*scale,bytes([0x44|dest<<3,0xb3,0xef])),(3,0,bytes([5|dest<<3])+bytes.fromhex('03002000')),(4,-17,bytes([0x80|dest<<3])+(-17).to_bytes(4,'little',signed=True))]
  for address,disp,encoding in forms:records.append(dict(pc=None,bytes_hex=(prefix+encoding).hex(),op=op,kind=1,dest=dest,source=0,address=address,disp=disp))
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
for record in records:
 ins=next(md.disasm(bytes.fromhex(record['bytes_hex']),0));assert ins.mnemonic==audit['records'][record['op']]['mnemonic'] and ins.size==len(bytes.fromhex(record['bytes_hex']));record['assembly']=ins.mnemonic+' '+ins.op_str
run=fixture(out/'fixture',records,original,srw);assert run.returncode==0,(run.stdout,run.stderr)
text=(out/'fixture/seg01_code.llinc').read_text();procedures=list(re.finditer(r'^proc fixture_(\d+) global[^\n]*\n(.*?)^endp$',text,re.M|re.S));assert len(procedures)==len(records)
negative=[]
for name,raw in [('mask','62f17d0978c0'),('wide','62f17d2878c0'),('sae','62f17d1878c0'),('zero','62f17d8878c0'),('extended','62d17d0878c0'),('reserved-vvvv','62f1750878c0'),('segment','6462f17d087800'),('address16','6762f17d087800'),('legacy66','6662f17d0878c0')]:
 run=fixture(out/('negative-'+name),[dict(bytes_hex=raw)],original,srw);assert run.returncode!=0,(name,run.stdout,run.stderr);negative.append(dict(name=name,bytes_hex=raw,exit=run.returncode))
support=R/'port/llasm-support';source=out/'bodies.llasm';source.write_text('include tribes_sse.llinc\n'+''.join('proc move_'+m[1]+' public\n'+m[2].split(';ret\n')[0]+'endp\n' for m in procedures));llasm=R/'generated/tools/llasm/llasm';ir=out/'bodies.ll';subprocess.run([str(llasm),'-m64','-ptrofs','-O2','-I',str(support),'-o',str(ir),str(source)],check=True)
(out/'moves.inc').write_text('typedef struct {unsigned op,kind,dest,source,address;int disp;} move_case;\n'+''.join('extern void move_'+str(i)+'(_cpu *);\n' for i in range(len(records)))+'static void (*const bodies[])(_cpu *)={'+','.join('move_'+str(i) for i in range(len(records)))+'};\nstatic const move_case moves[]={'+','.join('{'+','.join(str(v[k]) for k in ['op','kind','dest','source','address','disp'])+'}' for v in records)+'};\n')
reference=R/'generated/sse/quadword-conversion-cpu/macos';r=json.loads((reference/'result.json').read_text());assert r['pass'] and r['fixture_sha256']==sha(reference/'cases.inc')
for name,h in r['source_sha256'].items():assert sha(R/name)==h,name
(out/'cases.inc').write_bytes((reference/'cases.inc').read_bytes())
sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip()
c=R/'tests/quadword_conversion_lowering_test.c';sources=[ir,c,support/'tribes_quadword_conversion.c',R/'port/core/quadword_conversion.c'];exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-I',str(out)]+list(map(str,sources))+['-o',str(exe)];subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=[c,pathlib.Path(__file__).resolve(),support/'tribes_quadword_conversion.c',support/'tribes_sse.h',support/'tribes_sse.llinc',support/'llasm_cpu.h',R/'port/core/quadword_conversion.c',R/'port/core/quadword_conversion.h']
report={'pass':True,'state':'finished','cases':20472+(len(records)-7)*16,'blocks':len(records),'records':records,'negative':negative,'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'srw_sha256':sha(srw),'patch_sha256':build['patch_sha256'],'llasm_sha256':sha(llasm),'fixture_sha256':sha(out/'fixture/fixture.exe'),'emitted_body_sha256':sha(source),'ir_sha256':sha(ir),'binary_sha256':sha(exe),'rational_reference_sha256':sha(reference/'result.json'),'input_fixture_sha256':sha(out/'cases.inc'),'output':run.stdout.strip(),'scope':'Seven original EVEX128 conversion sites and synthetic allREGaliases/five memory forms, compressed signeddisp8 and unscaleddisp32, all8dest/source roles. Original bodies run all20472 rational vector cases; synthetic bodies16 samples each, guarded host alignment/full440CPU/stickyMXCSR. Not canonicalEVEX/preciseunmaskedWindowsfault/fullmodule/gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
