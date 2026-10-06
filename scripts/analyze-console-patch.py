#!/usr/bin/env python3
"""Reconstruct the first console-init CALL patch from captured native state.

This is a four-instruction original-x86 replay, not acceptance of the complete
routine, target behavior or an additional native code-publication phase.
"""
import hashlib,json,pathlib,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1]
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
out=R/'generated/integration-review/console-patch-analysis';out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
run=json.loads((native/'result.json').read_text());assert run['exit']==71 and run['result']['kind']=='console_patch_checkpoint'
for row in run['console_patch_checkpoint'].values():assert sha(R/row['path'])==row['sha256']
table=json.loads((native/'console-patch-pages.json').read_text());assert table['resume_pc']==0x1000292d
cpu=(native/'console-patch-cpu.bin').read_bytes();assert len(cpu)==440
registers=[X.UC_X86_REG_EAX,X.UC_X86_REG_ECX,X.UC_X86_REG_EDX,X.UC_X86_REG_EBX,X.UC_X86_REG_ESP,X.UC_X86_REG_EBP,X.UC_X86_REG_ESI,X.UC_X86_REG_EDI,X.UC_X86_REG_EFLAGS]
values=struct.unpack_from('<9I',cpu);assert values[0]==1
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row in table['ranges']:
 data=(native/row['file']).read_bytes();assert len(data)==row['size'];u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data)
for register,value in zip(registers,values):u.reg_write(register,value)
qol=R/'generated/srw/run-20261002T220330-ifc22/IFC22.dll';pe=pefile.PE(str(qol));image=pe.get_memory_mapped_image()
code=bytes(u.mem_read(0x1000292d,11));assert code==image[0x292d:0x2938]
before=bytes(u.mem_read(0x5d285f,6));trace=[];writes=[]
u.hook_add(unicorn.UC_HOOK_CODE,lambda u,pc,n,user:trace.append(dict(pc=pc,bytes=bytes(u.mem_read(pc,n)).hex())))
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,lambda u,access,address,size,value,user:writes.append(dict(pc=u.reg_read(X.UC_X86_REG_EIP),address=address,size=size,value=value)))
u.emu_start(0x1000292d,0x10002938,count=20)
after=bytes(u.mem_read(0x5d285f,6));assert len(trace)==4 and len(writes)==1 and before[0]==after[0]==0xe8 and before[5]==after[5]
expected=(0x10002550-0x5d2864)&0xffffffff;assert writes==[dict(pc=0x10002936,address=0x5d2860,size=4,value=expected)] and values[7]==expected
before_target=(0x5d2864+struct.unpack_from('<i',before,1)[0])&0xffffffff
after_target=(0x5d2864+struct.unpack_from('<i',after,1)[0])&0xffffffff
assert after_target==0x10002550 and before_target!=after_target
asm=[dict(pc=i.address,bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32).disasm(code,0x1000292d)]
report=dict(passed=True,native_resume_pc=table['resume_pc'],instructions=asm,trace=trace,writes=writes,call_pc=0x5d285f,before_bytes=before.hex(),after_bytes=after.hex(),before_target=before_target,after_target=after_target,inputs_sha256={str(p.relative_to(R)):sha(p) for p in [native/'result.json',native/'console-patch-cpu.bin',native/'console-patch-pages.json',qol,pathlib.Path(__file__).resolve()]},capture=run['console_patch_checkpoint'],scope='Four original x86 instructions from exact native post-VirtualProtect state establish the first changed CALL displacement. This is not native write acceptance, a complete console-init replay or validation of the newly called function. The seven-phase native publication limit is unchanged.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: captured original CALL redirects',hex(before_target),'->',hex(after_target),'at writer 0x10002936; native publication remains blocked')
