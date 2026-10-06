#!/usr/bin/env python3
"""Real producer -> branch pairs across conservative LLASM procedure boundaries."""
import argparse,ctypes,hashlib,json,pathlib,re,struct,subprocess,random
import pefile
from capstone import *
from capstone.x86 import *
from unicorn import *
from unicorn.x86_const import *
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();out=R/'generated/flags/seams';out.mkdir(parents=True,exist_ok=True)
pairs=[(0x48fba9,0x48fbaf),(0x4af861,0x4af863),(0x525a99,0x525aa8),(0x525aa3,0x525aa8),(0x52635b,0x526363),(0x526360,0x526363),(0x6665ac,0x6665b0),(0x6f82f7,0x6f8303),(0x6f82fe,0x6f8303),(0x6f8305,0x6f8311),(0x6f830c,0x6f8311),(0x6f8344,0x6f8350),(0x6f834b,0x6f8350),(0x6f8383,0x6f838f),(0x6f838a,0x6f838f),(0x6f9d62,0x6f9d6c),(0x6f9d68,0x6f9d6c),(0x6f9db6,0x6f9dbc),(0x6f9dba,0x6f9dbc)]
def norm(s):
 s=s.lower();s=re.sub(r'\bjnz\b','jne',s);s=re.sub(r'\bjz\b','je',s);s=re.sub(r'loc_([0-9a-f]+)',lambda m:'0x'+m[1],s);s=re.sub(r'\b(?:byte|word|dword|ptr|short|near)\b','',s);s=re.sub(r'\b(?:0x[0-9a-f]+|[0-9]+)\b',lambda m:str(int(m[0],16) if m[0].startswith('0x') else int(m[0])),s);s=re.sub(r'\s+','',s);return re.sub(r',-(\d+)$',lambda m:','+str((-int(m[1]))&0xffffffff),s)
pe=pefile.PE(str(R/'ref/control/Tribes2.exe'));image=pe.get_memory_mapped_image();md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True;needed=set(x for pair in pairs for x in pair);instructions={addr:next(md.disasm(pe.get_data(addr-0x400000,15),addr)) for addr in needed};keys=set(norm(i.mnemonic+' '+i.op_str) for i in instructions.values());positions={key:[] for key in keys}
for (addr,) in struct.iter_unpack('<I',(R/'generated/analysis/tribes2/instructions.u32').read_bytes()):
 ins=next(md.disasm(image[addr-0x400000:addr-0x400000+15],addr),None)
 if ins:
  key=norm(ins.mnemonic+' '+ins.op_str)
  if key in positions:positions[key].append(addr)
bodies={}
for chunk in (a.lift/'seg01_code.llinc').read_text().split('\n\n'):
 lines=chunk.splitlines();start=next((i for i,l in enumerate(lines) if l.startswith(';') and norm(l[1:]) in keys),None)
 if start is None:continue
 key=norm(lines[start][1:]);assert positions[key],key;addr=positions[key].pop(0)
 if addr in needed:bodies[addr]='\n'.join(lines[start+1:])
assert set(bodies)==needed
ll='include llasm.llinc\n'
for n,(producer,branch) in enumerate(pairs):
 body=re.sub(r'loc_([A-Fa-f0-9]+)',lambda m:'0x'+m[1],bodies[producer]);body=re.sub(r'(?m)^(?:tcall .*|endp)$','',body)
 conditional=re.sub(r'loc_[A-Fa-f0-9]+',f'true_{n}',bodies[branch]);conditional=re.sub(r'(?m)^(?:tcall .*|endp)$','',conditional)
 assert conditional.count('ctcall')==1,(hex(branch),conditional)
 ll+=f'proc writer_{n} public\n{body}\ntcall reader_{n}\nendp\nproc reader_{n} public\n{conditional}\ntcall false_{n}\nendp\nproc true_{n} public\nmov eax, 1\nendp\nproc false_{n} public\nmov eax, 0\nendp\n'
(out/'seams.llasm').write_text(ll);subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'seams.ll'),str(out/'seams.llasm')],check=True)
ir=(out/'seams.ll').read_text().replace(' fastcc ',' ccc ').replace('define hidden ccc void @writer_','define ccc void @writer_');(out/'seams.ll').write_text(ir);subprocess.run(['clang','-arch','arm64','-O2','-dynamiclib',str(out/'seams.ll'),'-o',str(out/'seams.dylib')],check=True)
class CPU(ctypes.Structure):
 _fields_=[('regs',ctypes.c_uint32*9),('top',ctypes.c_uint32),('sw',ctypes.c_uint32),('cw',ctypes.c_uint32),('mm',ctypes.c_uint64*8),('offset',ctypes.c_uint64),('result',ctypes.c_int64),('bottom',ctypes.c_void_p),('stacktop',ctypes.c_void_p),('tag',ctypes.c_uint32),('mode',ctypes.c_uint32),('exp',ctypes.c_uint16*8),('extended',ctypes.c_uint32),('dispatch',ctypes.c_uint32),('xmm',(ctypes.c_uint8*16)*8),('mxcsr',ctypes.c_uint32),('sse_reserved',ctypes.c_uint32),('ymm_hi',(ctypes.c_uint8*16)*8)]
lib=ctypes.CDLL(str(out/'seams.dylib'));u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,0x700000);u.mem_write(0x400000,image);buf=ctypes.create_string_buffer(0x700000);rng=random.Random(25034)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI,UC_X86_REG_EFLAGS];ri={r:i for i,r in enumerate(['eax','ecx','edx','ebx','esp','ebp','esi','edi'])};count=0;edge=[0,1,8,15,16,0x7fffffff,0x80000000,0xffffffff]
for idx,(producer,branch) in enumerate(pairs):
 ins=instructions[producer];fn=getattr(lib,f'writer_{idx}');fn.argtypes=[ctypes.POINTER(CPU)]
 for value in edge+[rng.getrandbits(32) for _ in range(64)]:
  for equal in [False,True]:
   c=CPU();c.offset=ctypes.addressof(buf)-0x400000
   for i in range(8):c.regs[i]=rng.getrandbits(32)
   c.regs[8]=0x202|rng.choice([0,0x8d5,0x41,0x884]);memory=[]
   # Choose inputs spanning equality, unsigned boundaries and signed boundaries.
   for j,op in enumerate(ins.operands):
    if op.type==X86_OP_REG:c.regs[ri[ins.reg_name(op.reg)]]=value if j==0 else (value if equal else value^0x80000001)
   if len(ins.operands)>1 and ins.operands[1].type==X86_OP_IMM and equal:
    v=ins.operands[1].imm&0xffffffff
    if ins.operands[0].type==X86_OP_REG:c.regs[ri[ins.reg_name(ins.operands[0].reg)]]=v
    else:value=v
   for j,op in enumerate(ins.operands):
    if op.type!=X86_OP_MEM:continue
    mem=op.mem;bn=ins.reg_name(mem.base);ind=ins.reg_name(mem.index)
    if ind:c.regs[ri[ind]]=0x100
    if bn:c.regs[ri[bn]]=(0x800001-mem.disp-(c.regs[ri[ind]]*mem.scale if ind else 0))&0xffffffff
    address=(mem.disp+(c.regs[ri[bn]] if bn else 0)+(c.regs[ri[ind]]*mem.scale if ind else 0))&0xffffffff
    v=(value if j==0 else (value if equal else value^0x80000001))&((1<<(8*op.size))-1);data=v.to_bytes(op.size,'little');u.mem_write(address,data);ctypes.memmove(ctypes.addressof(buf)+address-0x400000,data,len(data));memory.append((address,len(data)))
   for r,v in zip(regs,c.regs):u.reg_write(r,v)
   u.emu_start(producer,producer+ins.size,count=1);u.emu_start(branch,branch+instructions[branch].size,count=1);expected=[u.reg_read(r) for r in regs];expected[0]=int(u.reg_read(UC_X86_REG_EIP)!=branch+instructions[branch].size)
   before=bytes(c)[36:];fn(ctypes.byref(c));assert list(c.regs)==expected,(hex(producer),hex(branch),hex(value),equal,list(map(hex,c.regs)),list(map(hex,expected)));assert bytes(c)[36:]==before
   for address,sz in memory:assert ctypes.string_at(ctypes.addressof(buf)+address-0x400000,sz)==bytes(u.mem_read(address,sz))
   count+=1
result={'pass':True,'producer_branch_pairs':len(pairs),'distinct_previously_rejected_branches':len(set(b for _,b in pairs)),'cases':count,'library_sha256':hashlib.sha256((out/'seams.dylib').read_bytes()).hexdigest(),'lift':str(a.lift),'limits':['Actual selected producers and branches across procedure boundaries','Full intervening control-flow paths and whole-engine execution remain pending']};(out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
