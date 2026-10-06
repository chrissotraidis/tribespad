#!/usr/bin/env python3
"""Check original x86 IAT reads with Unicorn against independent flag/stack rules."""
import hashlib,json,pathlib,random,struct
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();native=R/'generated/import-slot-cmp/macos/result.json';report=json.loads(native.read_text());assert report['pass'];out=R/'generated/import-slot-cmp/reference';out.mkdir(exist_ok=True);rng=random.Random(0x51ad0487);u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x10001000,4096);u.mem_map(0x10129000,4096);regs=[X.UC_X86_REG_EAX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX,X.UC_X86_REG_EBX,X.UC_X86_REG_ESP,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI];vecs=[getattr(X,'UC_X86_REG_YMM'+str(i)) for i in range(8)];digest=hashlib.sha256();count=0
for block,r in enumerate(report['records']):
 raw=bytes.fromhex(r['bytes_hex']);u.mem_write(0x10001000,raw)
 for i in range(4096):
  memory=bytearray(rng.getrandbits(8) for _ in range(97));right=r['right'];values=[0,1,0xffffffff,0x80000000,0x7fffffff,0xfffa0000,0xfffe1000,0x55555555,0xaaaaaaaa,right,(right-1)&0xffffffff,(right+1)&0xffffffff];value=values[i%12] if i<1024 or i%12<9 else rng.getrandbits(32);struct.pack_into('<I',memory,17+r['slot']*4,value);wanted=bytearray(memory);base=0x1012968f;u.mem_write(base,bytes(memory));gp=[rng.getrandbits(32) for _ in range(8)];gp[4]=0x101296a0+i%61+4;vectors=[rng.getrandbits(256) for _ in range(8)];control=(block*4096+i)&65535;flags=0x202|(rng.getrandbits(32)&0xcd5)
  for reg,v in zip(regs,gp):u.reg_write(reg,v)
  for reg,v in zip(vecs,vectors):u.reg_write(reg,v)
  u.reg_write(X.UC_X86_REG_EFLAGS,flags);u.reg_write(X.UC_X86_REG_MXCSR,control);expectedgp=list(gp);expectedflags=flags
  if r['kind']:
   expectedgp[4]-=4;struct.pack_into('<I',wanted,17+i%61,value)
  else:
   result=(value-right)&0xffffffff;parity=(bin(result&255).count('1')%2)==0;status=int(value<right)|(int(parity)<<2)|((value^right^result)&16)|(int(result==0)<<6)|((result>>24)&128)|((((value^right)&(value^result))>>20)&2048);expectedflags=(flags&~0x8d5)|status
  u.emu_start(0x10001000,0x10001000+len(raw),count=1)
  actualgp=[u.reg_read(reg) for reg in regs];actualvectors=[u.reg_read(reg) for reg in vecs];actualflags=u.reg_read(X.UC_X86_REG_EFLAGS);actualcontrol=u.reg_read(X.UC_X86_REG_MXCSR);actualmemory=bytes(u.mem_read(base,len(memory)))
  assert actualgp==expectedgp and actualvectors==vectors and actualflags==expectedflags and actualcontrol==control and actualmemory==wanted,(block,i)
  digest.update(raw+struct.pack('<3I',value,right,expectedflags)+bytes(wanted));count+=1
result={'pass':True,'state':'finished','cases':count,'blocks':len(report['records']),'source_sha256':sha(pathlib.Path(__file__)),'native_report_sha256':sha(native),'corpus_results_sha256':digest.hexdigest(),'unicorn_version':unicorn.__version__,'scope':'Actual x86 byte forms through Unicorn with relocated guest addresses and mutable import-slot values. Independent integer subtraction flags and exact overlapping stack stores; all8 GPR/YMM, MXCSR, flags and guarded memory compared. Separate corpus from native ARM; no physical Intel/StackWalk64 implementation/full initialization/gameplay proof.'};(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: %d x86 import-slot CMP/PUSH reference cases'%count)
