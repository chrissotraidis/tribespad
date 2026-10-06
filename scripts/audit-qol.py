#!/usr/bin/env python3
"""Static IFC22 patch candidates, not a replacement for runtime patch tracing."""
import collections,json,pathlib,struct
import capstone,pefile
from capstone import x86_const as X
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=pefile.PE(str(ROOT/'ref/qol/IFC22.dll'));image=p.get_memory_mapped_image();base=p.OPTIONAL_HEADER.ImageBase
starts=struct.iter_unpack('<I',(ROOT/'generated/analysis/ifc22/instructions.u32').read_bytes())
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
writes=[];references=collections.Counter();imports={i.address:i.name.decode() if i.name else '#'+str(i.ordinal) for d in p.DIRECTORY_ENTRY_IMPORT for i in d.imports};boundaries=[]
for (a,) in starts:
 ins=next(md.disasm(image[a-base:a-base+16],a,count=1),None)
 if not ins:continue
 for op in ins.operands:
  if op.type==X.X86_OP_IMM and 0x401000<=op.imm<0x6ff612:references[op.imm]+=1
  if op.type==X.X86_OP_MEM and not op.mem.base and not op.mem.index and 0x401000<=op.mem.disp<0x6ff612 and op.access&capstone.CS_AC_WRITE:
   writes.append({'site':hex(a),'guest_target':hex(op.mem.disp),'width':op.size,'instruction':ins.mnemonic})
 if ins.mnemonic in ['call','jmp'] and ins.operands and ins.operands[0].type==X.X86_OP_MEM:
  name=imports.get(ins.operands[0].mem.disp)
  if name in ['VirtualProtect','VirtualAlloc','FlushInstructionCache','GetProcAddress','LoadLibraryA','LoadLibraryW','CreateThread']:
   boundaries.append({'site':hex(a),'import':name})
r={'direct_code_write_candidates':writes,'guest_code_immediate_candidates':{hex(k):v for k,v in sorted(references.items())},'patch_and_loader_boundaries':boundaries,'limitations':['Indirect pointer writes and generated trampolines are not resolved.','Static immediate references can also be integer constants.','Windows runtime initialization capture and differential control-flow validation are still required.']}
(ROOT/'generated/qol-patch-audit.json').write_text(json.dumps(r,indent=2)+'\n');print(len(writes),'direct guest-code write sites;',len(references),'guest-code immediate candidates;',len(boundaries),'patch/loader boundary sites')
