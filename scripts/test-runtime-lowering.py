#!/usr/bin/env python3
"""ARM64 oracle checks of relocated ADD and REPNE SCASW from the real image."""
import argparse,ctypes,hashlib,json,pathlib,re,struct,subprocess,random
import hashlib
import pefile
from capstone import *
from capstone.x86 import *
from unicorn import *
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();out=R/'generated/runtime-lowering';out.mkdir(parents=True,exist_ok=True)
addresses=[0x41af5e,0x443ecb,0x59528b,0x5952d2,0x59544b,0x595460,0x59570f,0x595724,0x6d5221,0x6e5d6a]
def norm(s):
 s=re.sub(r'loc_([0-9a-f]+)',lambda m:'0x'+m[1],s.lower());s=re.sub(r'\b(?:byte|word|dword|qword|ptr)\b','',s);s=re.sub(r'\b(?:0x[0-9a-f]+|[0-9]+)\b',lambda m:str(int(m[0],16) if m[0].startswith('0x') else int(m[0])),s);return re.sub(r'\s+','',s)
manifest=json.loads((R/'inputs-manifest.json').read_text());expected=next(m['sha256'] for m in manifest['modules'] if m['path']=='ref/control/Tribes2.exe')
assert hashlib.sha256((R/'ref/control/Tribes2.exe').read_bytes()).hexdigest()==expected,'original image identity mismatch'
assert json.loads((a.lift/'result.json').read_text())['image_sha256']==expected,'lift image identity mismatch'
pe=pefile.PE(str(R/'ref/control/Tribes2.exe'));md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True;sites={}
for addr in addresses:
 ins=next(md.disasm(pe.get_data(addr-0x400000,15),addr));key=norm(ins.mnemonic+(' '+ins.op_str if ins.mnemonic=='add' else ''));sites.setdefault(key,[]).append(ins)
blocks=[]
for chunk in (a.lift/'seg01_code.llinc').read_text().split('\n\n'):
 lines=chunk.splitlines();start=next((i for i,l in enumerate(lines) if l.startswith(';') and norm(l[1:]) in sites and sites[norm(l[1:])]),None)
 if start is None:continue
 key=norm(lines[start][1:]);body='\n'.join(lines[start+1:]);body=re.sub(r'loc_([0-9A-Fa-f]+)',lambda m:'0x'+m[1],body);blocks.append((body,sites[key].pop(0)))
assert len(blocks)==10 and not any(sites.values())
ll='include llasm.llinc\ninclude tribes_scan.llinc\n'
for i,(body,ins) in enumerate(blocks):ll+=f'proc test_{i} public\n{body}\nendp\n'
(out/'blocks.llasm').write_text(ll)
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True)
ir=(out/'blocks.ll').read_text().replace('define hidden fastcc void @test_','define ccc void @test_');(out/'blocks.ll').write_text(ir)
subprocess.run(['clang','-arch','arm64','-O2','-DPTROFS_64BIT','-Wall','-Wextra','-dynamiclib',str(out/'blocks.ll'),str(R/'port/llasm-support/tribes_scan.c'),'-o',str(out/'blocks.dylib')],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('mm',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
lib=ctypes.CDLL(str(out/'blocks.dylib'));u=Uc(UC_ARCH_X86,UC_MODE_32);base=0x400000;size=0x700000;u.mem_map(base,size);u.mem_map(0x10000,0x1000);buf=ctypes.create_string_buffer(size);rng=random.Random(25034)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];regindex={r:i for i,r in enumerate(['eax','ecx','edx','ebx','esp','ebp','esi','edi'])}
cases={'add':0,'repne scasw':0}
for idx,(body,ins) in enumerate(blocks):
 fn=getattr(lib,f'test_{idx}');fn.argtypes=[ctypes.POINTER(CPU)]
 if ins.mnemonic=='add':
  values=[0,1,0xffffffff,0x7fffffff,0x80000000,0xfffffff0,(-ins.operands[1].imm)&0xffffffff,(0x80000000-ins.operands[1].imm)&0xffffffff]+[rng.getrandbits(32) for _ in range(128)]
  scenarios=[(v,flags) for v in values for flags in [0x202,0xad7,0x643]]
 else:
  # Forward/backward, zero count, first/middle/last match and exhaustion.
  scenarios=[(n,df,where,scan) for n in [0,1,2,7,16] for df in [0,0x400] for where in [-1,0,1,3,15] for scan in [0,0xffff,0x8000,0x7fff,0x1234]]
 for scenario in scenarios:
  c=CPU();c.offset=ctypes.addressof(buf)-base;c.top=5;c.sw=0x4000;c.cw=0x37f;c.tag=0xffff
  for i in range(8):c.regs[i]=rng.getrandbits(32);c.mm[i]=rng.getrandbits(64)
  if ins.mnemonic=='add':
   v,flags=scenario;c.regs[8]=flags;mem=ins.operands[0].mem;bn=ins.reg_name(mem.base);address=0x800001;c.regs[regindex[bn]]=address-mem.disp
   memory=v.to_bytes(4,'little');start=address
  else:
   n,df,where,scan=scenario;c.regs[0]=(c.regs[0]&0xffff0000)|scan;c.regs[1]=n;c.regs[7]=0x800041;c.regs[8]=0x202|df|rng.choice([0,0x8d5,0x41,0x884]);start=0x800000
   words=[scan^0xffff]*80
   if where>=0:words[(c.regs[7]-start)//2+(-where if df else where)]=scan
   # Odd address intentionally exercises unaligned loads in both directions.
   start+=1;memory=b''.join(struct.pack('<H',v) for v in words)
   if n==0:c.regs[7]=0 # An unmapped pointer must never be dereferenced.
  u.mem_write(start,memory);ctypes.memmove(ctypes.addressof(buf)+start-base,memory,len(memory));before=bytes(c)[36:]
  for r,v in zip(regs,c.regs):u.reg_write(r,v)
  u.mem_write(0x10000,bytes(ins.bytes));u.emu_start(0x10000,0x10000+ins.size)
  expected=[u.reg_read(r) for r in regs];fn(ctypes.byref(c));actual=list(c.regs)
  assert actual==expected,(idx,ins.mnemonic,scenario,'regs',list(map(hex,actual)),list(map(hex,expected)))
  assert ctypes.string_at(ctypes.addressof(buf)+start-base,len(memory))==bytes(u.mem_read(start,len(memory))),(idx,scenario,'memory')
  assert bytes(c)[36:]==before,(idx,'FPU/host state')
  cases[ins.mnemonic]+=1
result={'pass':True,'sites':len(blocks),'cases':cases,'total_cases':sum(cases.values()),'lift':str(a.lift),'binary_sha256':hashlib.sha256((out/'blocks.dylib').read_bytes()).hexdigest(),'limits':['ADD flag hints force full arithmetic flags for testing','Only REPNE SCASW with 32-bit addresses/counts is implemented']};(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
