#!/usr/bin/env python3
"""Inspect the next reached CALL displacement using the generic native capture."""
import hashlib,json,pathlib,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1]
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
out=R/'generated/integration-review/next-call-analysis';out.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
run=json.loads((native/'result.json').read_text());result=run['result']
assert run['exit']==71 and result['kind']=='protection_checkpoint' and run['post_file_patch_publication']
assert (result['address'],result['bytes'],result['protection'])==(0x617c03,4,0x40)
for row in run['protection_checkpoint'].values():assert sha(R/row['path'])==row['sha256']
table=json.loads((native/'protection-pages.json').read_text());cpu=(native/'protection-cpu.bin').read_bytes();pc=result['resume_pc']
assert table['resume_pc']==pc and pc in [0x1000c3a1,0x10010bb1] and len(cpu)==440
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row in table['ranges']:
    data=(native/row['file']).read_bytes();assert len(data)==row['size'];u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data)
values=struct.unpack_from('<9I',cpu);assert values[0]==1 and values[3]==0x617c03
registers=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for reg,value in zip(registers,values):u.reg_write(reg,value)
stack=struct.unpack('<5I',u.mem_read(values[4]-20,20));assert stack[:4]==(pc,0x617c03,4,0x40)
assert bytes(u.mem_read(pc-2,2))==bytes.fromhex('ffd6')
pe=pefile.PE(str(R/'ref/product/IFC22.dll'));assert bytes(u.mem_read(pc,6))==pe.get_data(pc-0x10000000,6)
before=bytes(u.mem_read(0x617c02,5));assert before[0]==0xe8
old=(0x617c07+struct.unpack_from('<i',before,1)[0])&0xffffffff;assert old==0x58d900
target=(0x617c07+values[7])&0xffffffff;assert 0x10001000<=target<0x10130000
trace=[];writes=[]
u.hook_add(unicorn.UC_HOOK_CODE,lambda m,a,n,_:trace.append(dict(pc=a,bytes=bytes(m.mem_read(a,n)).hex())))
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,lambda m,access,a,n,v,_:writes.append(dict(pc=m.reg_read(X.UC_X86_REG_EIP),address=a,size=n,value=v)))
u.emu_start(pc,pc+6,count=10)
assert u.reg_read(X.UC_X86_REG_EIP)==pc+6 and len(trace)==3 and writes==[dict(pc=pc+4,address=0x617c03,size=4,value=values[7])]
after=bytes(u.mem_read(0x617c02,5));assert after==b'\xe8'+struct.pack('<I',values[7])
expected=bytearray(cpu);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in registers]);(out/'reference-cpu.bin').write_bytes(expected)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
report=dict(passed=True,pc=pc,writer=pc+4,continuation=pc+6,trace=trace,writes=writes,call_site=0x617c02,old_target=old,new_target=target,before_bytes=before.hex(),after_bytes=after.hex(),
            preceding_virtual_protect_stack=list(stack),target_entry_bytes=bytes(u.mem_read(target,32)).hex(),
            target_entry_disassembly=[dict(pc=i.address,bytes=i.bytes.hex(),assembly=i.mnemonic+' '+i.op_str) for i in md.disasm(bytes(u.mem_read(target,32)),target)],
            reference_cpu_sha256=sha(out/'reference-cpu.bin'),capture=run['protection_checkpoint'],
            inputs_sha256={str(p.relative_to(R)):sha(p) for p in [native/'result.json',native/'protection-cpu.bin',native/'protection-pages.json',R/'ref/product/IFC22.dll',pathlib.Path(__file__).resolve()]},
            scope='Original three-instruction store replay from the actual generic post-VirtualProtect snapshot. Captured stack and original CALL ESI verify resumed guest ABI. Target entry is descriptive only; no eleventh revision or target behavioral acceptance.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: actual writer',hex(pc+4),'redirects CALL 0x617c02 from 0x58d900 to',hex(target),'; publication remains blocked')
