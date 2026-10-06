#!/usr/bin/env python3
"""Compare captured original worker byte-CAS input/output with one x86 instruction."""
import hashlib,json,pathlib,struct
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();o=R/'generated/integration-review/locked-cmpxchg-byte/continuation';p=o/'worker-execution.json';d=json.loads(p.read_text());assert len(d['returns'])==1 and not d['suspended']
r=d['returns'][0];before=bytes.fromhex(r['cpu_hex']);after=bytes.fromhex(r['after_cpu_hex']);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
image=R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll';raw=pefile.PE(str(image)).get_data(0x7ff60,4);assert raw.hex()=='f00fb00e' and r['source']==1
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x1007f000,4096);u.mem_write(0x1007ff60,raw);u.mem_map(r['address']&~4095,4096);u.mem_write(r['address'],bytes.fromhex(r['before']))
for reg,v in zip(regs,struct.unpack_from('<9I',before)):u.reg_write(reg,v)
u.emu_start(0x1007ff60,0x1007ff64,count=1);expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);assert bytes(expected)==after and bytes(u.mem_read(r['address'],1)).hex()==r['after']
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text());assert sha(native/'bootstrap')==run['binary_sha256']
d.update(binary_sha256=sha(native/'bootstrap'),log_sha256=sha(o/'worker-lldb.log'),capture_script_sha256=sha(o/'capture_worker.py'),driver_sha256=sha(o/'run_capture.py'),verified_original_x86=True,verification_script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Actual original worker byte-CAS input and return match original x86 across440 CPU bytes and the accessed byte. No thread was manually suspended in this capture. No substituted API result or code write; debugger scheduling differs from ordinary launch. Bounded operand observation only, no whole-memory comparison.')
p.write_text(json.dumps(d,indent=2)+'\n');print('PASS: actual worker byte',r['before'],'->',r['after'],'and all440 CPU bytes match original x86')
