"""Restore exact segment stack effects in five pinned Miles callback bodies."""
import re,capstone

def correct(code,pe):
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);rows=[];lines=code.splitlines();result=[];pc=None;i=0
 while i<len(lines):
  line=lines[i];match=re.fullmatch(r'proc loc_([0-9A-F]+)',line)
  if match:pc=int(match[1],16)
  if pc is not None and 0x2114e027<=pc<0x2114e316 and line.startswith(';'):
   ins=next(md.disasm(pe.get_data(pc-pe.OPTIONAL_HEADER.ImageBase,15),pc));pc+=ins.size
   if ins.mnemonic in ('push','pop') and ins.op_str in ('ds','es'):
    original=';push byte 0' if ins.mnemonic=='push' else ';add esp, 4';lowering='PUSH 0' if ins.mnemonic=='push' else 'add esp, esp, 4'
    assert line==original and lines[i+1]==lowering,(hex(ins.address),line,lines[i+1])
    result+=[';'+ins.mnemonic+' '+ins.op_str,'call tribes_miles_'+ins.mnemonic+'_flat_segment $'];rows.append(dict(pc=ins.address,bytes=ins.bytes.hex(),operation=ins.mnemonic,segment=ins.op_str));i+=2;continue
  result.append(line);i+=1
 assert len(rows)==24,len(rows)
 return '\n'.join(result)+'\n',rows
