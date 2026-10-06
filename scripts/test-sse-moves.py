#!/usr/bin/env python3
"""Compare native legacy SSE move helpers with real x86 instruction execution.
Generate one shared C oracle corpus for macOS and sequential simulator runs.
This does not validate SRW lowering, floating arithmetic, VEX or guest SEH.
"""
import argparse,hashlib,json,pathlib,random,subprocess
import capstone,unicorn
from unicorn import x86_const as X
from sse_build_inputs import sse_inputs
R=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [d['udid'] for rows in inventory['devices'].values() for d in rows if d['state']=='Booted']==[a.simulator_device]
out=R/'generated/sse/moves'/(a.simulator_device or 'macos');out.mkdir(parents=True,exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x1000,4096);u.mem_map(0x2000,4096)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);rng=random.Random(25034);cases=[];counts={}
# Modes match the deliberately finite helper contract. Distinct legacy opcodes
# sharing a move width are still executed separately in the x86 oracle.
forms=[('movss','f30f10','f30f11',0),('movlpd','660f12','660f13',1),('movq','f30f7e','660fd6',2),('movd','660f6e','660f7e',3),('movups','0f10','0f11',4),('movdqu','f30f6f','f30f7f',4),('movaps','0f28','0f29',5)]
def corpus(name,prefix,kind,mode,dst,src,offset,iteration):
 initial=bytes(rng.getrandbits(8) for _ in range(128));memory=bytes(rng.getrandbits(8) for _ in range(64));value=rng.getrandbits(32)
 if iteration==0:initial=bytes(range(128));memory=bytes(range(64))
 if iteration==1:initial=b'\xff'*128;memory=b'\x00'*64
 flags=0x202|rng.choice([0,0x8d5,0x41,0x884]);mxcsr=rng.choice([0x1f80,0x9fc0,0x7f80,0x3f80])
 if kind in (0,1):modrm=dst<<3 # [eax] memory operand
 elif kind==2:modrm=0xc0|(dst<<3)|src
 elif kind==3:modrm=0xc0|(dst<<3) # xmm, eax
 elif kind==4:modrm=0xc0|(dst<<3) # eax, xmm (reg field is source)
 else:raise AssertionError(kind)
 code=bytes.fromhex(prefix)+bytes([modrm]);ins=next(decoder.disasm(code,0x1000));assert ins.mnemonic==name and ins.size==len(code)
 for i in range(8):u.reg_write(getattr(X,'UC_X86_REG_XMM'+str(i)),int.from_bytes(initial[i*16:i*16+16],'little'))
 u.reg_write(X.UC_X86_REG_EAX,value if kind==3 else 0x2000+offset);u.reg_write(X.UC_X86_REG_EFLAGS,flags);u.reg_write(X.UC_X86_REG_MXCSR,mxcsr)
 u.mem_write(0x2000,memory);u.mem_write(0x1000,code);u.emu_start(0x1000,0x1000+len(code))
 assert u.reg_read(X.UC_X86_REG_EFLAGS)==flags and u.reg_read(X.UC_X86_REG_MXCSR)==mxcsr
 expected=b''.join(u.reg_read(getattr(X,'UC_X86_REG_XMM'+str(i))).to_bytes(16,'little') for i in range(8));after=bytes(u.mem_read(0x2000,64));result=u.reg_read(X.UC_X86_REG_EAX) if kind==4 else 0
 cases.append((kind,mode,dst,src,offset,value,flags,mxcsr,result,initial,expected,memory,after));counts[name]=counts.get(name,0)+1
for name,load,store,mode in forms:
 for dst in range(8):
  for iteration in range(4):
   offset=16 if mode==5 else 1+iteration*3
   corpus(name,load,0,mode,dst,0,offset,iteration);corpus(name,store,1,mode,dst,0,offset,iteration)
   if mode==3:corpus(name,load,3,mode,dst,0,0,iteration);corpus(name,store,4,mode,dst,0,0,iteration)
  if mode not in (1,3):
   for src in range(8):
    for iteration in range(4):corpus(name,load,2,mode,dst,src,0,iteration)
# Also exercise the register-destination store opcode forms. Their reg field
# holds the source, unlike load-style encoding.
for name,load,store,mode in forms:
 if mode not in (0,2,4,5):continue
 for dst in range(8):
  for src in range(8):
   corpus(name,store,2,mode,src,dst,0,2)
   row=list(cases[-1]);row[2],row[3]=dst,src;cases[-1]=tuple(row)
def array(raw):return '{'+','.join(map(str,raw))+'}'
rows=[]
for row in cases:rows.append('{'+','.join([str(v)+'u' for v in row[:9]]+[array(v) for v in row[9:]])+'}')
table=out/'cases.inc';table.write_text('static const Case cases[]={\n'+',\n'.join(rows)+'\n};\n')
sdk_name,target=('iphonesimulator','arm64-apple-ios16-simulator') if a.simulator_device else ('macosx','arm64-apple-macos12');sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip();sources=[R/'port/llasm-support/tribes_sse.c',R/'tests/sse_moves_test.c'];runs=[]
math=sse_inputs('simulator' if a.simulator_device else 'macos')
for mode in (['normal'] if a.simulator_device else ['normal','tsan']):
 exe=out/mode;cmd=['clang','-target',target,'-isysroot',sdk,'-O2','-pthread','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-I',str(out)]
 if mode=='tsan':cmd+=['-fsanitize=thread']
 run=subprocess.run(cmd+math['compile_flags']+list(map(str,sources))+math['sources']+math['libraries']+['-o',str(exe)],capture_output=True,text=True);(out/(mode+'-compile.log')).write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
 run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60);(out/(mode+'-run.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
 runs.append({'mode':mode,'pass':True,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
paths=sources+[R/'port/llasm-support/llasm_cpu.h',R/'port/llasm-support/tribes_sse.h',pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'sse_math':math['evidence'],'target':target,'simulator_device':a.simulator_device,'cases':len(cases),'operations':counts,'oracle':'Actual legacy x86 instruction bytes in Unicorn '+unicorn.__version__,'corpus_sha256':hashlib.sha256(table.read_bytes()).hexdigest(),'source_sha256':{str(p.relative_to(R)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'runs':runs,'scope':'Native move helpers and full CPU/memory isolation, repeated on eight independent native threads; no emitted SRW instruction lowering, FP arithmetic, VEX, SEH or game-session claim'};report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
