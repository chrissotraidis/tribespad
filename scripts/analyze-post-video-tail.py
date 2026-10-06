#!/usr/bin/env python3
"""Inspect the reached CALL write and following conditional immediate patches."""
import hashlib,json,pathlib,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/post-video-tail';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();cap=json.loads((out/'capture.json').read_text());assert cap['pc']=='0x1005b4bb';cpu=(out/'cpu.bin').read_bytes();assert sha(out/'cpu.bin')==cap['cpu_sha256']
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);files=[out/'capture.json',out/'cpu.bin',out/'capture_tail.py',out/'run_capture.py',out/'tail-lldb.log']
for row in cap['ranges']:
 p=out/row['file'];assert sha(p)==row['sha256'];u.mem_map(row['address'],row['size']);u.mem_write(row['address'],p.read_bytes());files.append(p)
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
values=struct.unpack_from('<9I',cpu)
for reg,v in zip(regs,values):u.reg_write(reg,v)
assert values[0]==1 and values[3]==0x4023f3 and values[7]==0x1005af70-0x4023f7
image=R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll';pe=pefile.PE(str(image));assert bytes(u.mem_read(0x1005b4bb,0x36))==pe.get_data(0x5b4bb,0x36)
assert bytes(u.mem_read(0x4023f2,1))==b'\xe8';writes=[]
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,lambda m,a,p,n,v,_:writes.append(dict(pc=m.reg_read(X.UC_X86_REG_EIP),address=p,size=n,value=v,before=bytes(m.mem_read(p,n)).hex())))
u.emu_start(0x1005b4bb,0x1005b4bd,count=1);assert len(writes)==1 and writes[0]['address']==0x4023f3 and writes[0]['value']==values[7]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;records=[]
for name,entry,end,pc,address,size,value,target in [('loc_4023F0',0x4023f0,0x4023f7,0x1005b4bb,0x4023f3,4,values[7],0x1005af70),('loc_4B3C70',0x4b3c70,0x4b3c8a,0x1005b4d0,0x4b3c84,1,0x54,0x401fe0),('loc_4C65D0',0x4c65d0,0x4c65dd,0x1005b4ea,0x4c65d6,1,2,0x44bf20)]:
 raw=bytearray(u.mem_read(entry,end-entry))
 if size==4:raw[address-entry:address-entry+size]=bytes.fromhex(writes[0]['before'])
 before=bytes(raw);old=before[address-entry:address-entry+size];raw[address-entry:address-entry+size]=value.to_bytes(size,'little')
 a=list(md.disasm(before,entry));b=list(md.disasm(bytes(raw),entry));assert len(a)==len(b) and sum(x.bytes!=y.bytes for x,y in zip(a,b))==1
 assert b[-1].mnemonic=='call' and b[-1].operands[0].imm==target
 if size==1:
  writer=list(md.disasm(bytes(u.mem_read(pc,7)),pc));assert len(writer)==1 and writer[0].mnemonic=='mov' and writer[0].operands[0].mem.disp==address and writer[0].operands[1].imm==value
 records.append(dict(name=name,entry=entry,end=end,target=target,before_bytes=before.hex(),after_bytes=bytes(raw).hex(),write=dict(pc=pc,address=address,size=size,before=old.hex(),after=value.to_bytes(size,'little').hex()),observed_writer_reached=size==4))
span=[dict(pc=i.address,bytes=i.bytes.hex(),instruction=i.mnemonic+' '+i.op_str) for i in md.disasm(bytes(u.mem_read(0x1005b4bb,0x36)),0x1005b4bb)]
assert [i['pc'] for i in span if i['instruction'].startswith('call ')]==[0x1005b4ca,0x1005b4e4]
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text());assert run['binary_sha256']==sha(native/'bootstrap') and run['result']['kind']=='memory_violation' and '0x4023f3' in run['result']['reason'];assert 'Published second post-video NOP region after eight exact stores' in (out/'tail-lldb.log').read_text()
files += [native/'result.json',image,pathlib.Path(__file__).resolve()]
report=dict(passed=True,published=False,binary_sha256=run['binary_sha256'],records=records,reached_writes=writes,following_instructions=span,sources_sha256={str(p.relative_to(R)):sha(p) for p in files},scope='Actual native CPU before the CALL displacement write; isolated original-x86 store and all three engine preimages decoded. Two following byte writes are static candidates conditional on their real VirtualProtect results, not observed executions. No native writes or seventeenth revision admitted. No service return is fabricated.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: reached CALL writer and three candidate blocks; following two writes remain conditional/unobserved')
