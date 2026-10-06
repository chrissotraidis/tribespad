#!/usr/bin/env python3
"""Check native VMOVSS helpers against canonical x86 instructions via Rosetta.
Guest register roles are mapped to the reference's three fixed registers;
this tests all guest aliases without claiming decoder/emitted-body coverage.
"""
import argparse,hashlib,json,pathlib,random,struct,subprocess
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True))
 assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
out=R/'generated/sse/avx-ss'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True)
report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
oracle=R/'tests/vmovss_oracle.c';reference=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-mavx','-mno-red-zone','-O2','-Wall','-Wextra','-Werror',str(oracle),'-o',str(reference)],check=True)
rng=random.Random(25034);cases=[];requests=[]
def add(kind,dest,merge,source,iteration):
 initial=bytes(rng.getrandbits(8) for _ in range(256));memory=bytes(rng.getrandbits(8) for _ in range(64))
 if iteration==0:initial=bytes(256)
 elif iteration==1:initial=b'\xff'*256
 elif iteration==2:initial=bytes(range(256))
 offset=[1,2,3,7][iteration];flags=[0,0x8d5,0x41,0x884][iteration]
 control=0x1f80|(iteration<<13)|(0x8040 if iteration&1 else 0)|(0x3f if iteration&2 else 0)
 def vector(index):return initial[index*16:index*16+16]+initial[128+index*16:144+index*16]
 mapped=vector(dest)+vector(merge)+vector(source)
 word=struct.unpack_from('<I',memory,offset)[0]
 requests.append(f'{kind} {control:x} {flags:x} {word:x} '+' '.join(f'{v:016x}' for v in struct.unpack('<12Q',mapped))+'\n')
 cases.append((kind,dest,merge,source,offset,0x202|flags,control,initial,memory,mapped))
for kind in (0,1):
 for dest in range(8):
  for merge in range(8):
   for source in range(8):
    for iteration in range(4):add(kind,dest,merge,source,iteration)
for kind in (2,3):
 for index in range(8):
  for iteration in range(4):add(kind,index,0,index,iteration)
request=''.join(requests);(out/'requests.txt').write_text(request)
run=subprocess.run(['arch','-x86_64',str(reference)],input=request,text=True,capture_output=True,check=True,timeout=60)
(out/'reference.txt').write_text(run.stdout);replies=run.stdout.splitlines();assert len(replies)==len(cases)
def array(raw):return '{'+','.join(map(str,raw))+'}'
rows=[]
for case,reply in zip(cases,replies):
 kind,dest,merge,source,offset,flags,control,initial,memory,mapped=case
 values=[int(v,16) for v in reply.split()];assert len(values)==15
 observed=struct.pack('<12Q',*values[:12]);after_word,after_control,after_flags=values[12:]
 assert after_control==control and after_flags==(flags&0x8d5)
 # Independently check Intel's operand mapping before publishing the oracle.
 wanted=bytearray(mapped);wanted_word=struct.unpack_from('<I',memory,offset)[0]
 if kind in (0,1):wanted[:4]=mapped[64:68];wanted[4:16]=mapped[36:48];wanted[16:32]=bytes(16)
 elif kind==2:wanted[:4]=struct.pack('<I',wanted_word);wanted[4:32]=bytes(28)
 else:wanted_word=struct.unpack('<I',mapped[64:68])[0]
 assert observed==bytes(wanted) and after_word==wanted_word
 expected=bytearray(initial);after=bytearray(memory)
 if kind!=3:
  expected[dest*16:dest*16+16]=observed[:16]
  expected[128+dest*16:144+dest*16]=observed[16:32]
 else:after[offset:offset+4]=struct.pack('<I',after_word)
 rows.append('{'+','.join([str(v)+'u' for v in case[:7]]+[array(raw) for raw in [initial,expected,memory,after]])+'}')
table=out/'cases.inc';table.write_text('static const Case cases[]={\n'+',\n'.join(rows)+'\n};\n')
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12')
sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
sources=[R/'port/llasm-support/tribes_sse.c',R/'tests/avx_ss_test.c'];math=sse_inputs('simulator' if a.simulator_device else 'macos');runs=[]
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 run=subprocess.run(cmd+math['compile_flags']+list(map(str,sources))+math['sources']+math['libraries']+['-o',str(exe)],capture_output=True,text=True)
 (out/(mode+'-compile.log')).write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60)
 (out/(mode+'-run.log')).write_text(run.stdout+run.stderr)
 assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':sha(exe)})
paths=sources+[R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',R/'port/llasm-support/tribes_sse.llinc',pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'target':target,'simulator_device':a.simulator_device,'cases':len(cases),'register_cases':4096,'memory_cases':64,'runs':runs,'sse_math':math['evidence'],'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'corpus_sha256':sha(table),'oracle':{'kind':'Offline canonical VMOVSS register opcodes/load/store through Rosetta, mapped guest input roles','source_sha256':sha(oracle),'binary_sha256':sha(reference),'request_sha256':sha(out/'requests.txt'),'results_sha256':sha(out/'reference.txt'),'intel_mapping_agrees':True},'scope':'Native helpers and all guest register-role aliases, full CPU/guarded memory and thread isolation only; canonical reference uses distinct fixed registers. No decoder/emitted instruction, precise fault delivery, native whole DLL, physical Intel or gameplay claim'}
report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
