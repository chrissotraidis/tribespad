#!/usr/bin/env python3
"""Replay the next exact x86 patch prefix from the checked native checkpoint.
Stops before the next import; does not relax native code publication guards.
"""
import argparse,hashlib,json,pathlib,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('checkpoint',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();source=a.checkpoint.resolve();run=json.loads(source.read_text())
assert run['result']['kind']=='startup_patch_checkpoint' and run['result']['resume_pc']==0x10060c9a and run['late_patch_publication']
files={}
for name,row in run['startup_patch_checkpoint'].items():
 f=R/row['path'];assert sha(f)==row['sha256'] and f.stat().st_size==row['bytes'];files[name]=f
manifest=json.loads(files['startup-patch-pages.json'].read_text());cpu=files['startup-patch-cpu.bin'].read_bytes();assert len(cpu)==440
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row in manifest['ranges']:
 b=files[row['file']].read_bytes();assert len(b)==row['size'];u.mem_map(row['address'],row['size']);u.mem_write(row['address'],b)
regs=['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']
for i,name in enumerate(regs):u.reg_write(getattr(X,'UC_X86_REG_'+name),struct.unpack_from('<I',cpu,4*i)[0])
original=R/'ref/product/IFC22.dll';pe=pefile.PE(str(original));pc=0x10060c9a;end=0x10060cae
assert bytes(u.mem_read(pc,end-pc))==pe.get_data(pc-0x10000000,end-pc)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);instructions=[];writes=[]
def step(machine,address,size,data):
 raw=bytes(machine.mem_read(address,size));ins=next(decoder.disasm(raw,address));instructions.append(dict(pc=address,bytes_hex=raw.hex(),assembly=ins.mnemonic+' '+ins.op_str));assert len(instructions)<=10 and pc<=address<end

def write(machine,access,address,size,value,data):
 writes.append(dict(pc=machine.reg_read(X.UC_X86_REG_EIP),address=address,size=size,value=value&((1<<(8*size))-1),before=bytes(machine.mem_read(address,size)).hex()))
u.hook_add(unicorn.UC_HOOK_CODE,step);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write);before=bytes(u.mem_read(0x556d00,6));u.emu_start(pc,end,count=10)
assert u.reg_read(X.UC_X86_REG_EIP)==end and before==bytes.fromhex('ff15f0c67d00') and bytes(u.mem_read(0x556d00,6))==b'\x90'*6
assert [(w['pc'],w['address'],w['size'],w['value']) for w in writes]==[(0x10060ca8,0x556d00,4,0x90909090),(0x10060caa,0x556d04,2,0x9090)],writes
for w in writes:w['after']=bytes(u.mem_read(w['address'],w['size'])).hex()
report=dict(passed=True,source_checkpoint=str(source.relative_to(R)),source_checkpoint_sha256=sha(source),original_sha256=sha(original),native_resume_pc=pc,end_pc=end,instructions=instructions,writes=writes,final_general={name:u.reg_read(getattr(X,'UC_X86_REG_'+name)) for name in regs},input_provider_data=dict(address=0x7868d0,bytes_hex=bytes(u.mem_read(0x7868d0,10)).hex()),scope='Six original x86 instructions from the actual checked native CPU/memory snapshot. Exactly two writes remove one FreeLibrary call. Stops before LoadLibraryA and does not establish the rest of this startup patch phase, native publication or rendering.')
out=a.output.resolve();out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
