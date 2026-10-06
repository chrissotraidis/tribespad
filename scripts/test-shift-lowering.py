#!/usr/bin/env python3
"""Execute every actual scalar-shift/ROL/SHRD emitted block on ARM64."""
import argparse,ctypes,hashlib,json,pathlib,re,struct,subprocess,random
import pefile
from capstone import *
from capstone.x86 import *
from unicorn import *
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();out=R/'generated/shift/lowering';out.mkdir(parents=True,exist_ok=True)
ops=set('shl shr sar rol shrd'.split())
def norm(s):
 s=re.sub(r'loc_([0-9a-f]+)',lambda m:'0x'+m[1],s.lower());s=re.sub(r'\b(?:byte|word|dword|qword|ptr)\b','',s);s=re.sub(r'\b(?:0x[0-9a-f]+|[0-9]+)\b',lambda m:str(int(m[0],16) if m[0].startswith('0x') else int(m[0])),s);return re.sub(r'\s+','',s)
manifest=json.loads((R/'inputs-manifest.json').read_text());expected=next(m['sha256'] for m in manifest['modules'] if m['path']=='ref/control/Tribes2.exe');assert hashlib.sha256((R/'ref/control/Tribes2.exe').read_bytes()).hexdigest()==expected
lift_result=json.loads((a.lift/'result.json').read_text());assert lift_result['image_sha256']==expected and lift_result['full_flags']
pe=pefile.PE(str(R/'ref/control/Tribes2.exe'));image=pe.get_memory_mapped_image();md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True;sites={}
for (addr,) in struct.iter_unpack('<I',(R/'generated/analysis/tribes2/instructions.u32').read_bytes()):
 ins=next(md.disasm(image[addr-0x400000:addr-0x400000+15],addr),None)
 if ins and ins.mnemonic in ops:sites.setdefault(norm(ins.mnemonic+' '+ins.op_str),[]).append(ins)
sitecount=sum(map(len,sites.values()));blocks=[]
for chunk in (a.lift/'seg01_code.llinc').read_text().split('\n\n'):
 lines=chunk.splitlines();start=next((i for i,l in enumerate(lines) if l.startswith(';') and l[1:].split(' ')[0] in ops),None)
 if start is None:continue
 key=norm(lines[start][1:]);assert key in sites and sites[key],('no original',key);body='\n'.join(lines[start+1:]);assert 'call tb_shift_' in body and 'trap' not in body,(key,body)
 body=re.sub(r'loc_([0-9A-Fa-f]+)',lambda m:'0x'+m[1],body);blocks.append((key,body,sites[key].pop(0)))
assert len(blocks)==sitecount and not any(sites.values())
ll='include tribes_shift.llinc\n'
for i,(key,body,ins) in enumerate(blocks):ll+=f'proc test_{i} public\n{body}\nendp\n'
(out/'blocks.llasm').write_text(ll)
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-o',str(out/'blocks.ll'),str(out/'blocks.llasm')],check=True)
ir=(out/'blocks.ll').read_text().replace('define hidden fastcc void @test_','define ccc void @test_');(out/'blocks.ll').write_text(ir)
subprocess.run(['clang','-arch','arm64','-O2','-DPTROFS_64BIT','-dynamiclib',str(out/'blocks.ll'),str(R/'port/core/shift.c'),str(R/'port/llasm-support/tribes_shift.c'),'-o',str(out/'blocks.dylib')],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('mm',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mmx_active',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
lib=ctypes.CDLL(str(out/'blocks.dylib'));u=Uc(UC_ARCH_X86,UC_MODE_32);base=0x400000;size=0x700000;u.mem_map(base,size);u.mem_map(0x10000,0x1000);buf=ctypes.create_string_buffer(size);rng=random.Random(25034)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];regindex={r:i for i,r in enumerate(['eax','ecx','edx','ebx','esp','ebp','esi','edi'])};total=0
for idx,(key,body,ins) in enumerate(blocks):
 fn=getattr(lib,f'test_{idx}');fn.argtypes=[ctypes.POINTER(CPU)]
 for n in [0,1,7,8,15,16,17,31,32,63,255]:
  c=CPU();c.offset=ctypes.addressof(buf)-base;c.cw=0x37f;c.top=3;c.sw=0x4000;c.tag=0xffff
  for i in range(8):c.regs[i]=rng.getrandbits(32);c.mm[i]=rng.getrandbits(64)
  c.regs[1]=(c.regs[1]&0xffffff00)|n;c.regs[8]=0x202|rng.choice([0,0x8d5,0x41,0x884]);memory=[]
  for op in ins.operands:
   if op.type!=X86_OP_MEM:continue
   mem=op.mem;bn=ins.reg_name(mem.base);ind=ins.reg_name(mem.index)
   if bn:
    if ind:c.regs[regindex[ind]]=rng.randrange(0,1024)
    c.regs[regindex[bn]]=(0x800001-mem.disp-(c.regs[regindex[ind]]*mem.scale if ind else 0))&0xffffffff
   elif ind:c.regs[regindex[ind]]=(0x800008-mem.disp)//mem.scale
   address=(mem.disp+(c.regs[regindex[bn]] if bn else 0)+(c.regs[regindex[ind]]*mem.scale if ind else 0))&0xffffffff
   assert base<=address<base+size-8,(key,hex(address));data=rng.getrandbits(op.size*8).to_bytes(op.size,'little');u.mem_write(address,data);ctypes.memmove(ctypes.addressof(buf)+address-base,data,len(data));memory.append((address,op.size))
  count=(c.regs[1]&255) if ins.operands[-1].type==X86_OP_REG else ins.operands[-1].imm;count&=31;width=ins.operands[0].size*8;defined=0xffffffff
  if count:
   if ins.mnemonic=='rol':
    if count!=1:defined&=~0x800
   else:
    defined&=~0x10
    if count!=1:defined&=~0x800
    if ins.mnemonic in ['shl','shr'] and count>=width:defined&=~1
  for r,v in zip(regs,c.regs):u.reg_write(r,v)
  u.mem_write(0x10000,bytes(ins.bytes));u.emu_start(0x10000,0x10000+ins.size,count=1);expected_regs=[u.reg_read(r) for r in regs];preserved=bytes(c)[36:];fn(ctypes.byref(c));actual=list(c.regs)
  assert actual[:8]==expected_regs[:8],(idx,key,n,'GPR',list(map(hex,actual)),list(map(hex,expected_regs)))
  assert (actual[8]&defined)==(expected_regs[8]&defined),(idx,key,n,'flags',hex(actual[8]),hex(expected_regs[8]),hex(defined))
  assert bytes(c)[36:]==preserved,(idx,key,'FPU/host state')
  for address,sz in memory:assert ctypes.string_at(ctypes.addressof(buf)+address-base,sz)==bytes(u.mem_read(address,sz)),(idx,key,n,'memory')
  total+=1
result={'pass':True,'original_sites':sitecount,'emitted_blocks':len(blocks),'cases':total,'library_sha256':hashlib.sha256((out/'blocks.dylib').read_bytes()).hexdigest(),'lift':str(a.lift),'limits':['Architecturally undefined flag bits excluded','Individual emitted blocks; full engine and control-flow seam checks remain pending']};(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
