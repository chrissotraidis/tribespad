#!/usr/bin/env python3
"""Reconstruct the next CALL displacement, without admitting a ninth phase."""
import json, pathlib, struct
import capstone, pefile, unicorn
from unicorn import x86_const as X
from console_fade_inputs import R, sha

native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
out=R/'generated/integration-review/console-animation-analysis'; out.mkdir(parents=True,exist_ok=True)
run=json.loads((native/'result.json').read_text())
assert run['exit']==71 and run['result']['kind']=='console_animation_checkpoint' and run['console_fade_publication']
for row in run['console_animation_checkpoint'].values(): assert sha(R/row['path'])==row['sha256']
table=json.loads((native/'console-animation-pages.json').read_text()); assert table['resume_pc']==0x10002bc0
cpu=(native/'console-animation-cpu.bin').read_bytes(); assert len(cpu)==440
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row in table['ranges']:
    data=(native/row['file']).read_bytes(); assert len(data)==row['size']; u.mem_map(row['address'],row['size']); u.mem_write(row['address'],data)
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
values=struct.unpack_from('<9I',cpu); assert values[0]==1
for reg,value in zip(regs,values): u.reg_write(reg,value)
qol=R/'ref/product/IFC22.dll'; pe=pefile.PE(str(qol))
assert bytes(u.mem_read(0x10002bc0,11))==pe.get_data(0x2bc0,11)
before=bytes(u.mem_read(0x67cc94,5)); trace=[]; writes=[]
u.hook_add(unicorn.UC_HOOK_CODE,lambda u,pc,n,_:trace.append(dict(pc=pc,bytes=bytes(u.mem_read(pc,n)).hex())))
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,lambda u,access,address,size,value,_:writes.append(dict(pc=u.reg_read(X.UC_X86_REG_EIP),address=address,size=size,value=value)))
u.emu_start(0x10002bc0,0x10002bcb,count=20)
after=bytes(u.mem_read(0x67cc94,5)); value=(0x10002450-0x67cc99)&0xffffffff
assert len(trace)==4 and writes==[dict(pc=0x10002bc9,address=0x67cc95,size=4,value=value)]
assert values[7]==value and before[0]==after[0]==0xe8
assert (0x67cc99+struct.unpack_from('<i',before,1)[0])&0xffffffff==0x67cfb0
assert (0x67cc99+struct.unpack_from('<i',after,1)[0])&0xffffffff==0x10002450
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
target=list(decoder.disasm(pe.get_data(0x2450,0x9b),0x10002450))
report=dict(passed=True,trace=trace,writes=writes,before_bytes=before.hex(),after_bytes=after.hex(),before_target=0x67cfb0,after_target=0x10002450,
    target_instructions=[dict(pc=i.address,bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in target],
    inputs_sha256={str(p.relative_to(R)):sha(p) for p in [native/'result.json',native/'console-animation-cpu.bin',native/'console-animation-pages.json',qol,pathlib.Path(__file__).resolve()]},
    capture=run['console_animation_checkpoint'],
    scope='Four original x86 instructions from exact native post-VirtualProtect state establish the next CALL redirect. Target disassembly is descriptive, not behavioral validation. No ninth revision, new target acceptance or complete console initializer equivalence.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: captured CALL 0x67cc94 redirects 0x67cfb0 to0x10002450 at writer0x10002bc9; ninth revision remains blocked')
