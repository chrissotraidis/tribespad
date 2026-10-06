#!/usr/bin/env python3
"""Replay only the first call redirect from the actual guarded native snapshot."""
import argparse,hashlib,json,pathlib,struct
import pefile,unicorn,capstone
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();p=argparse.ArgumentParser();p.add_argument('checkpoint',type=pathlib.Path);p.add_argument('--phase',choices=['library','followup'],default='library');p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();src=a.checkpoint.resolve();report=json.loads(src.read_text());phase=a.phase;start,end,site,writer,expected_old,expected_new=(0x10012cf6,0x10012cfc,0x539064,0x10012cfa,0x6c6900,0x100bbfb0) if phase=='library' else (0x10027e5e,0x10027e69,0x5ae362,0x10027e67,0x5ae660,0x100275c0)
assert report['result']['kind']==phase+'_patch_checkpoint' and report['result']['resume_pc']==start and report['input_code_publication']
if phase=='followup':assert report['library_patch_publication']
files={}
for name,row in report[phase+'_patch_checkpoint'].items():
 path=R/row['path'];assert sha(path)==row['sha256'] and path.stat().st_size==row['bytes'];files[name]=path
pages=json.loads(files[phase+'-patch-pages.json'].read_text());cpu=files[phase+'-patch-cpu.bin'].read_bytes();assert len(cpu)==440
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row in pages['ranges']:
 data=files[row['file']].read_bytes();assert len(data)==row['size'];u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data)
regs=['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']
for i,name in enumerate(regs):u.reg_write(getattr(X,'UC_X86_REG_'+name),struct.unpack_from('<I',cpu,i*4)[0])
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));assert bytes(u.mem_read(start,end-start))==pe.get_data(start-0x10000000,end-start)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);instructions=[];writes=[]
def instruction(machine,pc,size,_):
 ins=next(decoder.disasm(bytes(machine.mem_read(pc,size)),pc));assert start<=pc<end;instructions.append(dict(pc=pc,bytes=ins.bytes.hex(),assembly=ins.mnemonic+' '+ins.op_str))
def write(machine,access,address,size,value,_):writes.append(dict(pc=machine.reg_read(X.UC_X86_REG_EIP),address=address,size=size,value=value,before=bytes(machine.mem_read(address,size)).hex()))
u.hook_add(unicorn.UC_HOOK_CODE,instruction);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write);before=bytes(u.mem_read(site,5));u.emu_start(start,end,count=5);after=bytes(u.mem_read(site,5));assert u.reg_read(X.UC_X86_REG_EIP)==end and len(writes)==1
w=writes[0];assert (w['pc'],w['address'],w['size'])==(writer,site+1,4);w['after']=bytes(u.mem_read(w['address'],4)).hex();assert before[0]==after[0]==0xe8
old_target=(site+5+int.from_bytes(before[1:],'little'))&0xffffffff;new_target=(site+5+int.from_bytes(after[1:],'little'))&0xffffffff;assert old_target==expected_old and new_target==expected_new
out=a.output.resolve();out.parent.mkdir(parents=True,exist_ok=True);result=dict(passed=True,native_checkpoint=str(src.relative_to(R)),native_checkpoint_sha256=sha(src),original_sha256=sha(original),script_sha256=sha(pathlib.Path(__file__).resolve()),instructions=instructions,writes=writes,old_target=old_target,new_target=new_target,next_pc=end,scope='Bounded original instructions from the real native snapshot; one existing direct-call displacement changes. No Windows service model, later patch-sequence coverage or native activation claim. Next analyze the full enclosing call-redirection routine before preparing another finite publication.')
out.write_text(json.dumps(result,indent=2)+'\n');print('PASS: first call redirect',hex(old_target),'->',hex(new_target),'at',hex(w['address']))
