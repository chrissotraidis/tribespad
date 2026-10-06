#!/usr/bin/env python3
"""Validate the actual Tribes SETAE and JCXZ emitted blocks on ARM64."""
import argparse,ctypes,json,pathlib,re,struct,subprocess
import hashlib
import pefile
from capstone import *
from unicorn import *
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();out=R/'generated/conditionals';out.mkdir(parents=True,exist_ok=True)
manifest=json.loads((R/'inputs-manifest.json').read_text());expected=next(m['sha256'] for m in manifest['modules'] if m['path']=='ref/control/Tribes2.exe')
assert hashlib.sha256((R/'ref/control/Tribes2.exe').read_bytes()).hexdigest()==expected,'original image identity mismatch'
assert json.loads((a.lift/'result.json').read_text())['image_sha256']==expected,'lift image identity mismatch'
pe=pefile.PE(str(R/'ref/control/Tribes2.exe'));image=pe.get_memory_mapped_image();md=Cs(CS_ARCH_X86,CS_MODE_32);sites={'setae':[],'jcxz':[]}
for (addr,) in struct.iter_unpack('<I',(R/'generated/analysis/tribes2/instructions.u32').read_bytes()):
 ins=next(md.disasm(image[addr-0x400000:addr-0x400000+15],addr),None)
 if ins and ins.mnemonic in sites:sites[ins.mnemonic].append(ins)
blocks=[];ll='define CF 1\n'
for chunk in (a.lift/'seg01_code.llinc').read_text().split('\n\n'):
 lines=chunk.splitlines();start=next((i for i,l in enumerate(lines) if l.startswith(';setae ') or l.startswith(';jcxz ')),None)
 if start is None:continue
 name=lines[start][1:].split()[0];ins=sites[name].pop(0);body='\n'.join(lines[start+1:]);idx=len(blocks)
 if name=='jcxz':
  assert body.count('ctcallz')==1,'duplicate conditional branch'
  body=re.sub(r'loc_[A-Fa-f0-9]+',f'target_{idx}',body)
  body+=f'\ntcall fall_{idx}';ll+=f'proc target_{idx} public\nmov eax, 1\nendp\nproc fall_{idx} public\nmov eax, 0\nendp\n'
 ll+=f'proc test_{idx} public\n{body}\nendp\n';blocks.append(ins)
assert len(blocks)==6 and not any(sites.values())
(out/'blocks.llasm').write_text(ll)
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-O2','-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True)
ir=(out/'blocks.ll').read_text().replace(' fastcc ',' ccc ').replace('define hidden ccc void @test_','define ccc void @test_');(out/'blocks.ll').write_text(ir)
subprocess.run(['clang','-arch','arm64','-O2','-dynamiclib',str(out/'blocks.ll'),'-o',str(out/'blocks.dylib')],check=True)
lib=ctypes.CDLL(str(out/'blocks.dylib'));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,0x1000);u.mem_map(0x400000,0x700000);u.mem_write(0x400000,image)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];count=0
for idx,ins in enumerate(blocks):
 fn=getattr(lib,f'test_{idx}');fn.argtypes=[ctypes.c_void_p]
 for mask in range(64):
  flags=0x202|sum(bit for i,bit in enumerate([1,4,16,64,128,2048]) if mask&(1<<i))
  for cx in [0,1,0xffff,0x10000,0xffff0000,0xffffffff]:
   state=(ctypes.c_uint32*40)();initial=[0x12345678,cx,0x87654321,0xdeadbeef,0x800000,0x7fffff,0x800001,0x800003,flags]
   for i,v in enumerate(initial):state[i]=v;u.reg_write(regs[i],v)
   if ins.mnemonic=='jcxz':
    u.emu_start(ins.address,ins.address+ins.size,count=1);expected=initial[:];expected[0]=int(u.reg_read(UC_X86_REG_EIP)!=ins.address+ins.size)
   else:
    u.mem_write(0x10000,bytes(ins.bytes));u.emu_start(0x10000,0x10000+ins.size);expected=[u.reg_read(r) for r in regs]
   fn(state);assert list(state)[:9]==expected,(idx,ins.mnemonic,hex(flags),hex(cx),list(state)[:9],expected);count+=1
result={'pass':True,'actual_sites':len(blocks),'cases':count,'lift':str(a.lift),'coverage':'All six Tribes SETAE/JCXZ sites; all combinations of arithmetic flags and CX zero with nonzero upper ECX'};(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
