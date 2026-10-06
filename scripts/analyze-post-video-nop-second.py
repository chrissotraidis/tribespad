#!/usr/bin/env python3
"""Replay the captured original27-byte removal without admitting native writes."""
import hashlib,json,pathlib,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/post-video-nop-second';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();cap=json.loads((out/'capture.json').read_text());assert cap['pc']=='0x1005b429';before=(out/'cpu.bin').read_bytes();assert sha(out/'cpu.bin')==cap['cpu_sha256']
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row in cap['ranges']:
 p=out/row['file'];assert sha(p)==row['sha256'];u.mem_map(row['address'],row['size']);u.mem_write(row['address'],p.read_bytes())
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for reg,v in zip(regs,struct.unpack_from('<9I',before)):u.reg_write(reg,v)
assert u.reg_read(X.UC_X86_REG_EAX)==1
image=R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll';pe=pefile.PE(str(image));assert bytes(u.mem_read(0x1005b429,0x22))==pe.get_data(0x5b429,0x22)
trace=[];writes=[];original=bytes(u.mem_read(0x402540,0x36));u.hook_add(unicorn.UC_HOOK_CODE,lambda m,a,n,_:trace.append(dict(pc=a,bytes=bytes(m.mem_read(a,n)).hex())))
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,lambda m,access,a,n,v,_:writes.append(dict(pc=m.reg_read(X.UC_X86_REG_EIP),address=a,size=n,value=v,before=bytes(m.mem_read(a,n)).hex())))
u.emu_start(0x1005b429,0x1005b44b,count=32);assert len(trace)==10 and len(writes)==8 and bytes(u.mem_read(0x402543,27))==b'\x90'*27
assert [(r['address'],r['size']) for r in writes]==[(0x402543+i*4,4) for i in range(6)]+[(0x40255b,2),(0x40255d,1)]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);ins=[dict(pc=i.address,bytes=i.bytes.hex(),instruction=i.mnemonic+' '+i.op_str) for i in md.disasm(original,0x402540)]
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text());assert run['binary_sha256']==sha(native/'bootstrap')
assert 'Published first post-video NOP region after eight exact stores' in (out/'nop-lldb.log').read_text()
report=dict(passed=True,binary_sha256=run['binary_sha256'],published=False,resume_pc=0x1005b429,end_pc=0x1005b44b,trace=trace,writes=writes,original_engine=ins,before_bytes=original.hex(),after_bytes=bytes(u.mem_read(0x402540,0x36)).hex(),sources_sha256={str(p.relative_to(R)):sha(p) for p in [out/'capture.json',out/'cpu.bin',out/'00402000.bin',out/'1005b000.bin',out/'capture_nop.py',out/'run_capture.py',out/'nop-lldb.log',native/'result.json',image,pathlib.Path(__file__).resolve()]},notes=['Original engine basic block0x402540 begins with ADD ESP,20 that remains. Its following five pushes and CALL0x426650 are removed.','The next original entry0x40255b loses its ADD ESP,20, but its following pushes/CALL at0x40255e onward remain. Both blocks need variants before the next original registration.','No second-region native NOP store or sixteenth revision is accepted. A registration follows before the CALL redirect at0x4023f3.'])
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: eight original stores replace27 bytes; two affected engine blocks identified, no native publication')
