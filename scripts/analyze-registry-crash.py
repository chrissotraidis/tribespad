#!/usr/bin/env python3
"""Reproduce the original QoL caller's unchecked failed-query pointer use."""
import hashlib,json,pathlib,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/registry-crash'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();cpu=(out/'cpu.bin').read_bytes();stack=(out/'stack.bin').read_bytes();stack_base=0x2fe000
engine=R/'ref/control/Tribes2.exe';qol=R/'generated/srw/run-20261002T220330-ifc22/IFC22.dll'
e=pefile.PE(str(engine)).get_memory_mapped_image();q=pefile.PE(str(qol)).get_memory_mapped_image()
# Find the saved formatter-call return in the captured original guest stack.
returns=[stack_base+i for i in range(0,len(stack)-4,4) if struct.unpack_from('<I',stack,i)[0]==0x100608af]
assert len(returns)==1;ret=returns[0];ebp=struct.unpack_from('<I',stack,ret-stack_base-4)[0];esp=ret+12
local=ebp-0x14;stale=struct.unpack_from('<I',stack,local-stack_base)[0];assert 0x400000<=stale<0x400000+len(e)
bad=struct.unpack_from('<I',e,stale-0x400000)[0];assert bad==struct.unpack_from('<I',cpu,28)[0]
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
u.mem_map(stack_base,len(stack));u.mem_write(stack_base,stack)
u.mem_map(stale&~4095,4096);u.mem_write(stale&~4095,e[(stale&~4095)-0x400000:(stale&~4095)-0x400000+4096])
u.mem_map(0x10060000,4096);u.mem_write(0x10060000,q[0x60000:0x61000])
u.reg_write(X.UC_X86_REG_EBP,ebp);u.reg_write(X.UC_X86_REG_ESP,esp);u.reg_write(X.UC_X86_REG_EAX,0xfffffffe)
trace=[]
u.hook_add(unicorn.UC_HOOK_CODE,lambda u,pc,n,user:trace.append(dict(pc=pc,bytes=bytes(u.mem_read(pc,n)).hex())))
u.emu_start(0x1006089e,0x100608ad,count=20)
sp=u.reg_read(X.UC_X86_REG_ESP);args=struct.unpack('<II',u.mem_read(sp,8));assert args==(0x101ca874,bad) and len(trace)==4
asm=[dict(pc=i.address,bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32).disasm(q[0x60880:0x608af],0x10060880)]
report=dict(passed=True,captured_scan_guest_pointer=bad,stale_cpu_info_pointer=stale,caller_frame=ebp,reconstructed_post_query_esp=esp,format_string=q[0x1ca874:0x1ca88a].split(b'\0')[0].decode(),original_caller=asm,replayed=trace,formatter_arguments=list(args),sources_sha256={str(p.relative_to(R)):sha(p) for p in [engine,qol,out/'cpu.bin',out/'stack.bin',pathlib.Path(__file__).resolve()]},scope='Four original x86 caller instructions replayed using the preserved outer stack frame after a failed CPU-information query. EAX error is overwritten without a test; stale local points to engine instruction bytes, which become the same invalid string pointer observed by native SCASB. This local reconstruction is not whole-prefix x86 execution.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: original caller reproduces invalid string pointer',hex(bad),'from stale output',hex(stale))
