#!/usr/bin/env python3
"""Fail-closed AOT lift of audited integer leaf routines; emitted game code stays ignored.
This is a validation tool, not the whole-game runtime. Unsupported instructions abort generation.
"""
import hashlib,json,pathlib
import capstone,pefile
from capstone import x86_const as X
ROOT=pathlib.Path(__file__).resolve().parents[1]
REGS={'eax':(0,0,32),'ecx':(1,0,32),'edx':(2,0,32),'ebx':(3,0,32),'esp':(4,0,32),'ebp':(5,0,32),'esi':(6,0,32),'edi':(7,0,32)}
for n,(idx,_,_) in list(REGS.items()):
 REGS[n[1:]]=(idx,0,16)
 if idx<4:REGS[n[1]+'l']=(idx,0,8);REGS[n[1]+'h']=(idx,8,8)

def main():
 exe=ROOT/'ref/control/Tribes2.exe';digest=hashlib.sha256(exe.read_bytes()).hexdigest()
 expected=next(m['sha256'] for m in json.loads((ROOT/'inputs-manifest.json').read_text())['modules'] if m['path']=='ref/control/Tribes2.exe')
 if digest!=expected:raise SystemExit('Tribes2.exe hash mismatch')
 pe=pefile.PE(str(exe));image=pe.get_memory_mapped_image();base=pe.OPTIONAL_HEADER.ImageBase
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
 def reg(op):return REGS[md.reg_name(op.reg)]
 def addr(op):
  v=[f'0x{op.mem.disp&0xffffffff:x}u']
  for r,m in [(op.mem.base,1),(op.mem.index,op.mem.scale)]:
   if r:v.append(f's->r[{REGS[md.reg_name(r)][0]}]*{m}u')
  if op.mem.segment:raise ValueError('Segment memory unsupported')
  return '(uint32_t)('+ '+'.join(v)+')'
 def read(op):
  if op.type==X.X86_OP_IMM:return f'0x{op.imm&((1<<(op.size*8))-1):x}u'
  if op.type==X.X86_OP_MEM:return f'tb_read(s,{addr(op)},{op.size})'
  idx,shift,width=reg(op);return f'((s->r[{idx}]>>{shift})&0x{(1<<width)-1:x}u)'
 def write(op,v):
  if op.type==X.X86_OP_MEM:return f'tb_write(s,{addr(op)},{op.size},{v});'
  idx,shift,width=reg(op)
  if width==32:return f's->r[{idx}]={v};'
  mask=((1<<width)-1)<<shift
  return f's->r[{idx}]=(s->r[{idx}]&0x{(~mask)&0xffffffff:x}u)|(({v}&0x{(1<<width)-1:x}u)<<{shift});'
 targets={'tb_crc32':(0x6c32f0,0x6c344d),'tb_game_version':(0x6949c0,0x6949c6)}
 out=['#include "slice_runtime.h"']
 for name,(lo,hi) in targets.items():
  insns=list(md.disasm(image[lo-base:hi-base],lo));starts={i.address for i in insns}
  if not insns or insns[-1].address+insns[-1].size!=hi:raise ValueError('Partial function decode')
  out.append(f'void {name}(TBState *s) {{ uint32_t a,b,v;')
  for i in insns:
   ops=i.operands;m=i.mnemonic;out.append(f'L_{i.address:x}:;')
   if m in ['nop']:continue
   if m=='mov':out.append(write(ops[0],read(ops[1])))
   elif m=='lea':out.append(write(ops[0],addr(ops[1])))
   elif m=='push':out.append(f'a={read(ops[0])};s->r[4]-=4;tb_write(s,s->r[4],4,a);')
   elif m=='pop':out.append(f'a=tb_read(s,s->r[4],4);s->r[4]+=4;'+write(ops[0],'a'))
   elif m in ['xor','and','add','sub','cmp','test','inc','dec','shr']:
    width=ops[0].size*8;mask=(1<<width)-1
    out.append(f'a={read(ops[0])};b='+ ('1u;' if m in ['inc','dec'] else read(ops[1])+';'))
    if m in ['xor','and','test']:
     op='^' if m=='xor' else '&';out.append(f'v=(a{op}b)&0x{mask:x}u;tb_logic(s,v,{width});')
    elif m=='shr':out.append(f'v=a>>(b&31);tb_shr(s,a,b,v,{width});')
    else:
     subtract=m in ['sub','cmp','dec'];out.append(f'v=(a'+('-' if subtract else '+')+f'b)&0x{mask:x}u;tb_arith(s,a,b,v,{width},{int(subtract)},{int(m in ["inc","dec"])});')
    if m not in ['cmp','test']:out.append(write(ops[0],'v'))
   elif m in ['je','jne','jb','jae','jmp']:
    t=ops[0].imm
    if t not in starts:raise ValueError(f'Branch outside slice: {t:x}')
    c={'je':'s->zf','jne':'!s->zf','jb':'s->cf','jae':'!s->cf','jmp':'1'}[m]
    out.append(f'if({c}) goto L_{t:x};')
   elif m=='ret':out.append('s->pc=tb_read(s,s->r[4],4);s->r[4]+=4;return;')
   else:raise ValueError(f'Unsupported {i.address:x}: {m} {i.op_str}')
  out.append('abort();}')
 dest=ROOT/'generated/slices';dest.mkdir(parents=True,exist_ok=True)
 (dest/'tribes_slices.c').write_text('\n'.join(out)+'\n')
 (dest/'identity.json').write_text(json.dumps({'image_sha256':digest,'functions':{n:[hex(lo),hex(hi)] for n,(lo,hi) in targets.items()},'generator_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()},indent=2)+'\n')
 print('Lifted original CRC32 and build-version routines; no dynamic x86 execution in generated code')
if __name__=='__main__':main()
