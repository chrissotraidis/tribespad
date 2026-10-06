#!/usr/bin/env python3
"""Replay the reached CALL store from captured native state without publishing it."""
import hashlib,json,pathlib,re,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1]
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
out=R/'generated/integration-review/post-file-patch';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
run=json.loads((native/'result.json').read_text())
assert run['exit']==71 and run['result']['kind']=='post_file_patch_checkpoint' and run['console_animation_publication']
for row in run['post_file_patch_checkpoint'].values():assert sha(R/row['path'])==row['sha256']
table=json.loads((native/'post-file-patch-pages.json').read_text());cpu=(native/'post-file-patch-cpu.bin').read_bytes()
assert table['resume_pc']==0x10015d0a and len(cpu)==440
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row in table['ranges']:
    data=(native/row['file']).read_bytes();assert len(data)==row['size']
    u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data)
registers=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
values=struct.unpack_from('<9I',cpu);assert values[0]==1
for reg,value in zip(registers,values):u.reg_write(reg,value)
pe=pefile.PE(str(R/'ref/product/IFC22.dll'))
assert bytes(u.mem_read(0x10015d0a,11))==pe.get_data(0x15d0a,11)
before=bytes(u.mem_read(0x5bff78,5));assert before[0]==0xe8
old_target=(0x5bff7d+struct.unpack_from('<i',before,1)[0])&0xffffffff;assert old_target==0x4267a0
trace=[];writes=[]
u.hook_add(unicorn.UC_HOOK_CODE,lambda m,pc,n,_:trace.append(dict(pc=pc,bytes=bytes(m.mem_read(pc,n)).hex())))
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,lambda m,access,address,size,value,_:writes.append(dict(pc=m.reg_read(X.UC_X86_REG_EIP),address=address,size=size,value=value)))
u.emu_start(0x10015d0a,0x10015d15,count=20)
assert u.reg_read(X.UC_X86_REG_EIP)==0x10015d15 and len(trace)==4
displacement=(0x10015a10-0x5bff7d)&0xffffffff
assert writes==[dict(pc=0x10015d13,address=0x5bff79,size=4,value=displacement)]
after=bytes(u.mem_read(0x5bff78,5));assert after==b'\xe8'+struct.pack('<I',displacement)
final=bytearray(cpu);struct.pack_into('<9I',final,0,*[u.reg_read(reg) for reg in registers]);(out/'reference-cpu.bin').write_bytes(final)
pending=[]
for slot,entry in [(0x101d6654,0x58a470),(0x101d6650,0x58a4a0),(0x101d667c,0x5fe3b0)]:
    pointer=struct.unpack('<I',u.mem_read(slot,4))[0];assert pointer==entry
    pending.append(dict(slot=slot,pointer=pointer,entry_bytes=bytes(u.mem_read(entry,16)).hex()))
dll=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/module.ll';text=dll.read_text()
target_names=[n for n in re.findall(r'^define hidden fastcc void @(loc_[0-9A-F]+)\(',text,re.M) if 0x10015a10<=int(n[4:],16)<0x10015b1c]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
report=dict(passed=True,trace=trace,writes=writes,before_bytes=before.hex(),after_bytes=after.hex(),old_target=old_target,new_target=0x10015a10,
            target_definitions=target_names,target_instructions=[dict(pc=i.address,bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in md.disasm(pe.get_data(0x15a10,0x10c),0x10015a10)],
            pending_hook_entries=pending,reference_cpu_sha256=sha(out/'reference-cpu.bin'),capture=run['post_file_patch_checkpoint'],
            inputs_sha256={str(p.relative_to(R)):sha(p) for p in [native/'result.json',native/'post-file-patch-pages.json',native/'post-file-patch-cpu.bin',R/'ref/product/IFC22.dll',dll,pathlib.Path(__file__).resolve()]},
            scope='Four original x86 instructions replayed from the captured post-VirtualProtect native state. One CALL displacement changes; descriptive target disassembly and prior pending pointers retained. No tenth revision, target behavioral acceptance or complete initializer equivalence.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: captured CALL 0x5bff78 redirects 0x4267a0 to 0x10015a10; tenth revision remains unimplemented')
