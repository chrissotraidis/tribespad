#!/usr/bin/env python3
"""Inspect the reached high-byte compare and replay it from actual native state."""
import json,pathlib,re,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1]
from console_animation_inputs import sha
native=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';out=R/'generated/integration-review/highbyte-compare';out.mkdir(exist_ok=True)
run=json.loads((native/'result.json').read_text());assert run['exit']==71 and run['result']['kind']=='instruction' and run['result']['index']==0x10115878
assert run['console_animation_publication']
for row in run['instruction_checkpoint'].values():assert sha(R/row['path'])==row['sha256']
table=json.loads((native/'instruction-pages.json').read_text());cpu=(native/'instruction-cpu.bin').read_bytes();assert len(cpu)==440 and table['resume_pc']==0x10115878
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for r in table['ranges']:
    data=(native/r['file']).read_bytes();assert len(data)==r['size'];u.mem_map(r['address'],len(data));u.mem_write(r['address'],data)
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
values=struct.unpack_from('<9I',cpu)
for reg,value in zip(regs,values):u.reg_write(reg,value)
assert bytes(u.mem_read(0x10115878,2))==bytes.fromhex('382f')
memory_value=bytes(u.mem_read(values[7],1))[0];source_value=(values[1]>>8)&255
u.emu_start(0x10115878,0x1011587a,count=1)
after=[u.reg_read(reg) for reg in regs];assert after[:8]==list(values[:8])
expected=bytearray(cpu);struct.pack_into('<9I',expected,0,*after)
(out/'reference-cpu.bin').write_bytes(expected)
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
source=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/module.ll';pe=pefile.PE(str(R/'ref/product/IFC22.dll'))
pcs=sorted(set(map(int,re.findall(r'call void @halopad_trap_unimplemented\(i32 (\d+)\)',source.read_text()))));rows=[]
for pc in pcs:
    ins=next(decoder.disasm(pe.get_data(pc-0x10000000,15),pc),None)
    if ins and ins.mnemonic=='cmp' and ins.operands[0].type==capstone.x86.X86_OP_MEM and ins.operands[0].size==1 and ins.operands[1].type==capstone.x86.X86_OP_REG and ins.reg_name(ins.operands[1].reg) in ['ah','ch','dh','bh']:
        rows.append(dict(pc=pc,bytes=ins.bytes.hex(),assembly=ins.mnemonic+' '+ins.op_str))
report=dict(passed=True,pc=0x10115878,bytes='382f',instruction='cmp byte ptr [edi], ch',memory_address=values[7],memory_value=memory_value,high_byte_value=source_value,flags_before=values[8],flags_after=after[8],
    original_registers_unchanged=True,existing_trap_sites=len(pcs),missing_memory_highbyte_cmp=rows,
    inputs_sha256={str(p.relative_to(R)):sha(p) for p in [native/'result.json',native/'instruction-pages.json',native/'instruction-cpu.bin',source,R/'ref/product/IFC22.dll',R/'generated/tools/SRW-tribes/SR_full_llasm_instr.c',pathlib.Path(__file__).resolve()]},
    reference_cpu_sha256=sha(out/'reference-cpu.bin'),capture=run['instruction_checkpoint'],
    scope='Original x86 comparison from captured native pre-trap state; current compiler lacks memory/high-byte-register CMP emission. One such trapped site in current DLL. No native instruction fix, continuation or gameplay acceptance.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: original CMP memory=%u CH=%u flags=%#x; compiler fix remains pending'%(memory_value,source_value,after[8]))
