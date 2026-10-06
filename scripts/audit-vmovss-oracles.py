#!/usr/bin/env python3
"""Check VEX references before using them to validate native VMOVSS."""
import hashlib,json,pathlib,random,struct,subprocess
import capstone,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/sse/vmovss-reference';out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text(json.dumps({'state':'running','pass':False})+'\n')
source=R/'tests/vmovss_oracle.c';binary=out/'oracle-x86'
subprocess.run(['clang','-arch','x86_64','-mavx','-mno-red-zone','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
rng=random.Random(25034);cases=[];requests=[]
for mode in range(4):
 for i in range(32):
  initial=bytes(rng.getrandbits(8) for _ in range(96))
  if i==0:initial=bytes(96)
  elif i==1:initial=b'\xff'*96
  elif i==2:initial=bytes(range(96))
  memory=rng.getrandbits(32);flags=(0,0x8d5,0x41,0x884)[i%4]
  control=0x1f80|((i%4)<<13)|(0x8040 if i&4 else 0)|(0x3f if i&8 else 0)
  cases.append((mode,control,flags,memory,initial))
  requests.append(f'{mode} {control:x} {flags:x} {memory:x} '+' '.join(f'{v:016x}' for v in struct.unpack('<12Q',initial))+'\n')
request=''.join(requests);(out/'requests.txt').write_text(request)
run=subprocess.run(['arch','-x86_64',str(binary)],input=request,text=True,capture_output=True,check=True,timeout=60);(out/'rosetta-results.txt').write_text(run.stdout)
replies=run.stdout.splitlines();assert len(replies)==len(cases)
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x1000,4096);u.mem_map(0x2000,4096)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
codes=['c5f210c2','c5f211d0','c5fa1000','c5fa1110'];differences={};examples=[]
for case,reply in zip(cases,replies):
 mode,control,flags,memory,initial=case
 fields=[int(v,16) for v in reply.split()];observed=struct.pack('<12Q',*fields[:12]);after_memory,after_control,after_flags=fields[12:]
 expected=bytearray(initial);wanted_memory=memory
 # This bit mapping is taken from Intel MOVSS/VMOVSS operand pseudocode.
 if mode in (0,1):expected[:4]=initial[64:68];expected[4:16]=initial[36:48];expected[16:32]=bytes(16)
 elif mode==2:expected[:4]=struct.pack('<I',memory);expected[4:32]=bytes(28)
 else:wanted_memory=struct.unpack('<I',initial[64:68])[0]
 assert observed==bytes(expected) and (after_memory,after_control,after_flags)==(wanted_memory,control,flags)
 for reg in range(3):u.reg_write(getattr(X,'UC_X86_REG_YMM'+str(reg)),int.from_bytes(initial[reg*32:reg*32+32],'little'))
 u.reg_write(X.UC_X86_REG_EAX,0x2003);u.reg_write(X.UC_X86_REG_MXCSR,control);u.reg_write(X.UC_X86_REG_EFLAGS,0x202|flags);u.mem_write(0x2003,struct.pack('<I',memory))
 code=bytes.fromhex(codes[mode]);ins=next(decoder.disasm(code,0x1000));assert ins.mnemonic=='vmovss' and ins.size==4
 u.mem_write(0x1000,code);u.ctl_remove_cache(0x1000,0x1004);u.emu_start(0x1000,0x1004,count=1)
 actual=b''.join(u.reg_read(getattr(X,'UC_X86_REG_YMM'+str(reg))).to_bytes(32,'little') for reg in range(3))
 bad=actual!=observed or u.reg_read(X.UC_X86_REG_MXCSR)!=control or u.reg_read(X.UC_X86_REG_EFLAGS)!=(0x202|flags) or bytes(u.mem_read(0x2003,4))!=struct.pack('<I',wanted_memory)
 differences[mode]=differences.get(mode,0)+int(bad)
 if bad and len(examples)<8:examples.append({'mode':mode,'bytes_hex':code.hex(),'initial_hex':initial.hex(),'unicorn_hex':actual.hex(),'rosetta_hex':observed.hex()})
report={'state':'finished','pass':True,'cases':len(cases),'rosetta_matches_intel_operand_mapping':True,'unicorn_differences_by_mode':differences,'examples':examples,'unicorn_version':unicorn.__version__,'source_sha256':sha(source),'driver_sha256':sha(pathlib.Path(__file__)),'binary_sha256':sha(binary),'request_sha256':sha(out/'requests.txt'),'rosetta_results_sha256':sha(out/'rosetta-results.txt'),'intel_reference':'https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2b-manual.pdf','scope':'Offline VEX references and Intel operand mapping only; native VMOVSS helper/decoder/lowering remains unfinished; Rosetta is not physical Intel proof'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='examples'},indent=2))
