#!/usr/bin/env python3
"""Capture proof for the original clock CALL patch; admit no code writes."""
import datetime,hashlib,json,pathlib,struct,subprocess
import capstone
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/clock-patch-checkpoint-20261003';assert not (out/'checkpoint.json').exists()
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text());assert run['exit']==71 and run['initialized'] and run['result']['engine_entered']
assert run['result']['kind']=='protection_checkpoint' and run['result']['address']==0x58a966
assert run['binary_sha256']==sha(native/'bootstrap') and run['external_build_sha256']==sha(native/'guarded/result.json')
for name,row in run['protection_checkpoint'].items():assert sha(R/row['path'])==row['sha256'],name
table=json.loads((native/'protection-pages.json').read_text());assert table['resume_pc']==0x10060806
pages=[(row,(native/row['file']).read_bytes()) for row in table['ranges']]
cpu=(native/'protection-cpu.bin').read_bytes();assert len(cpu)==440
def read(address,size):
    for row,data in pages:
        at=row['address']
        if at<=address and address+size<=at+len(data):return data[address-at:address-at+size]
    raise AssertionError((hex(address),size))
target=0x1005f0d0
calls=[]
for address,writer in [(0x58a965,0x1006080f),(0x50cb12,0x1006086c)]:
    before=read(address,5);assert before[0]==0xe8
    old=(address+5+int.from_bytes(before[1:],'little',signed=True))&0xffffffff;assert old==0x55b800
    calls.append(dict(address=address,writer=writer,old_target=old,target=target,before=before.hex(),after=(b'\xe8'+struct.pack('<I',(target-address-5)&0xffffffff)).hex()))
assert struct.unpack_from('<I',cpu)[0]==1
assert struct.unpack_from('<I',cpu,7*4)[0]==(target-0x58a96a)&0xffffffff
# Replay only the first original store suffix, with no invented API result.
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for row,data in pages:u.mem_map(row['address'],len(data));u.mem_write(row['address'],data)
registers=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for reg,value in zip(registers,struct.unpack_from('<9I',cpu)):u.reg_write(reg,value)
trace=[];writes=[]
def code(machine,address,size,_):
    assert 0x10060806<=address<0x10060811
    trace.append(dict(pc=address,bytes=bytes(machine.mem_read(address,size)).hex()))
def write(machine,access,address,size,value,_):
    writes.append(dict(pc=machine.reg_read(X.UC_X86_REG_EIP),address=address,size=size,before=bytes(machine.mem_read(address,size)).hex(),after=value.to_bytes(size,'little').hex()))
u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write)
u.emu_start(0x10060806,0x10060811,count=8)
assert u.reg_read(X.UC_X86_REG_EIP)==0x10060811 and len(trace)==4 and len(writes)==1
assert writes[0]==dict(pc=0x1006080f,address=0x58a966,size=4,before=calls[0]['before'][2:],after=calls[0]['after'][2:])
d=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
instructions=[dict(pc=i.address,bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in d.disasm(read(0x100607b3,0xcb),0x100607b3)]
analysis=dict(first_native_resume=table['resume_pc'],calls=calls,first_store_reference=dict(instructions=trace,writes=writes),original_patch_instructions=instructions,
    scope='Actual native post-protection state; four-instruction original-x86 first store replay. Second CALL identified statically in the same initializer. No target/caller ARM64 equivalence, second native publication or thirteenth revision acceptance yet.')
files=[]
def add(p,rel):
    dst=out/rel;dst.parent.mkdir(parents=True,exist_ok=True)
    if not dst.exists():subprocess.run(['cp','-c',str(p),str(dst)],check=True)
    assert sha(p)==sha(dst),rel
    files.append(dict(original=str(p.relative_to(R)),path=rel,sha256=sha(dst)))
for name,row in run['protection_checkpoint'].items():add(R/row['path'],'capture/'+name)
for n in ['bootstrap','result.json','stdout.log','stderr.log','patch-inputs.json','guarded/result.json','guarded/policy.json','support/result.json']:add(native/n,'native/'+n)
for p in sorted(native.glob('*.ll')):add(p,'native/llvm/'+p.name)
for n in ['scripts/freeze-clock-patch-capture.py','scripts/test-qol-native-bootstrap.py','scripts/sdl_backend_inputs.py','tests/qol_native_bootstrap.c']:add(R/n,'sources/'+n)
add(R/'generated/integration-review/sdl-backend-checkpoint-20261003/checkpoint.json','dependencies/sdl-backend.json')
(out/'analysis.json').write_text(json.dumps(analysis,indent=2)+'\n')
report=dict(passed=True,created_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),files=files,analysis_sha256=sha(out/'analysis.json'),scope=analysis['scope'])
(out/'checkpoint.json').write_text(json.dumps(report,indent=2)+'\n')
for row in files:assert sha(out/row['path'])==row['sha256']
print('PASS: frozen and verified',len(files),'files; first clock store replay and paired CALL identification')
