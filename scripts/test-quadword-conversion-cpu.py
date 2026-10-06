#!/usr/bin/env python3
"""Direct native packed conversion helpers, not emitted instruction execution."""
import argparse,hashlib,json,pathlib,random,struct,subprocess
from quadword_conversion_reference import oracle
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args();platform='simulator' if a.simulator_device else 'macos'
out=R/'generated/sse/quadword-conversion-cpu'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);(out/'result.json').write_text(json.dumps({'pass':False,'state':'running'})+'\n')
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
rng=random.Random(0x12864);cases=[]
for op in range(7):
 if op<2:edge=[0,0x80000000,1,0x80000001,0x3f000000,0xbf000000,0x3f800000,0xbf800000,0x5effffff,0x5f000000,0x5f7fffff,0x5f800000,0xdf000000,0x7f800000,0xff800000,0x7fc00001,0x7f800001];bits=32
 elif op<4:edge=[0,1,0x8000000000000000,0x8000000000000001,0x3fe0000000000000,0xbfe0000000000000,0x3ff0000000000000,0xbff0000000000000,0x43dfffffffffffff,0x43e0000000000000,0x43efffffffffffff,0x43f0000000000000,0xc3e0000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000001,0x7ff0000000000001];bits=64
 else:edge=[0,1,2**64-1,2**63,2**63-1,2**53-1,2**53,2**53+1,2**53+3,2**24-1,2**24,2**24+1,2**24+3];bits=64
 pairs=[(x,y) for x in edge for y in edge]+[(rng.getrandbits(bits),rng.getrandbits(bits)) for _ in range(128)]
 for x,y in pairs:
  for rounding in range(4):
   for daz in range(2):
    control=0x1f80|(rounding<<13)|(daz<<6)|((len(cases)%2)<<15)|(len(cases)%64)
    left,lf=oracle(op,x,control);right,rf=oracle(op,y,control)
    input=struct.pack('<2I',x,y)+b'\xa6'*8 if op<2 else struct.pack('<2Q',x,y)
    output=struct.pack('<2I',left,right)+bytes(8) if op==6 else struct.pack('<2Q',left,right)
    cases.append((op,control,lf|rf,input,output))
def array(value):return '{'+','.join(str(x) for x in value)+'}'
(out/'cases.inc').write_text('struct conversion_case {unsigned op,control,raised;unsigned char input[16],output[16];};\nstatic const struct conversion_case cases[]={\n'+''.join('{%du,%du,%du,%s,%s},\n'%(op,control,raised,array(inp),array(result)) for op,control,raised,inp,result in cases)+'};\n')
sdkname='iphonesimulator' if a.simulator_device else 'macosx';target='arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12';sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip()
sources=[R/'tests/quadword_conversion_cpu_test.c',R/'port/llasm-support/tribes_quadword_conversion.c',R/'port/core/quadword_conversion.c'];exe=out/'native';cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-I',str(out)]+list(map(str,sources))+['-o',str(exe)];
if not a.simulator_device:cmd.insert(1,'-DTRIBESPAD_CPU_HOST_FAULT_TESTS')
subprocess.run(cmd,check=True)
run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=120);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);(out/'run.log').write_text(run.stdout)
paths=sources+[R/'port/core/quadword_conversion.h',R/'port/llasm-support/tribes_sse.h',R/'port/llasm-support/llasm_cpu.h',R/'scripts/quadword_conversion_reference.py',pathlib.Path(__file__).resolve()]
report={'pass':True,'state':'finished','cases':len(cases)*81,'packed_vectors':len(cases),'protected_page_cases':len(cases),'isolated_exception_publication_checks':0 if a.simulator_device else 2,'platform':platform,'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'fixture_sha256':sha(out/'cases.inc'),'binary_sha256':sha(exe),'compile_command':cmd,'output':run.stdout.strip(),'scope':'Direct seven unmasked128-bit conversion helpers, all64 REG aliases and16 MEM alignments per packed case plus inaccessible-page exact8/16byte reads. Independent rational lane oracle, sticky masked IE/PE, four rounding modes, DAZ/FTZ, mixed masked exceptions, full440CPU/allvectorbytes/memory. Host-only isolated unmasked aborts preserve destination; simulator unmasked fault publication not exercised. No precise unmasked packed priority/Windows delivery, canonicalEVEX, decoder/lowering, fullmodule/gameplay proof.'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout.strip())
