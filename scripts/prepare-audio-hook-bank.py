#!/usr/bin/env python3
"""Validate the live7-hook queue and strictly compile its pending trampoline bank."""
import hashlib,json,pathlib,struct,capstone,pefile,unicorn
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();capture=R/'generated/integration-review/audio-tables/execution';out=R/'generated/integration-review/audio-hook-bank';out.mkdir(exist_ok=True)
cap=json.loads((capture/'pending-hooks.json').read_text());assert len(cap['pending_hooks'])==7 and cap['binary_sha256']==json.loads((capture/'full-result.json').read_text())['binary_sha256']
bank=capture/'pending-bank.bin';assert sha(bank)==cap['bank_sha256'];data=bank.read_bytes();md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
# Execute the actual small-fill implementation to retain its real store PCs,
# widths and order. These sizes do not call an external service.
image_path=R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll';pe=pefile.PE(str(image_path));image=pe.get_memory_mapped_image();u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x10000000,(len(image)+4095)&~4095);u.mem_write(0x10000000,image);u.mem_map(0x200000,65536);u.mem_map(0x400000,0x300000)
fill_events=[]
def instruction(machine,address,size,_):
 if address==0x200100:machine.emu_stop()
def written(machine,access,address,size,value,_):
 if 0x400000<=address<0x700000:fill_events.append(dict(pc=machine.reg_read(UC_X86_REG_EIP),address=address,size=size,before=int.from_bytes(machine.mem_read(address,size),'little'),value=value&((1<<(size*8))-1)))
u.hook_add(unicorn.UC_HOOK_CODE,instruction);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,written)
records=[];writes=[]
for item in cap['pending_hooks']:
 address=int(item['fields'][4],16);target=int(item['fields'][3],16);b=bytes.fromhex(item['trampoline'])[:72];assert data[address-0x3c0000:address-0x3c0000+72]==b
 continuation,detour=struct.unpack_from('<II',b,64);n=continuation-target;assert 5<=n<=22 and b[30]==b[54]==n
 before=bytes.fromhex(item['target_bytes'])[:n];assert before==b[32:32+n]
 original=list(md.disasm(before,target));moved=list(md.disasm(b[:n+5],address));assert sum(i.size for i in original)==n and sum(i.size for i in moved)==n+5 and len(moved)==len(original)+1
 assert moved[-1].mnemonic=='jmp' and moved[-1].operands[0].imm==continuation
 for a,z in zip(original,moved[:-1]):
  assert a.mnemonic==z.mnemonic and a.size==z.size
  if a.group(capstone.CS_GRP_JUMP) or a.group(capstone.CS_GRP_CALL):
   assert a.op_str==z.op_str,(hex(target),a.op_str,z.op_str)
  else:assert a.bytes==z.bytes
 after=b'\xe9'+((detour-target-5)&0xffffffff).to_bytes(4,'little')+b'\xcc'*(n-5)
 records.append(dict(address=address,original_target=target,continuation=continuation,detour_target=detour,moved_code_bytes=n,code_hex=b[:n+5].hex(),restore_hex=before.hex(),after_hex=after.hex(),pending_record=int(item['address'],16),callback_slot=int(item['fields'][2],16)))
 writes += [dict(pc=0x10072860,address=target,size=1,before=before[0],value=0xe9),dict(pc=0x10072865,address=target+1,size=4,before=int.from_bytes(before[1:5],'little'),value=int.from_bytes(after[1:5],'little'))]
 if n>5:
  fill_events.clear();u.mem_write(target,before);u.mem_write(0x208000,struct.pack('<4I',0x200100,target+5,0xcc,n-5));u.reg_write(UC_X86_REG_ESP,0x208000)
  u.emu_start(0x100fbbd0,0,count=1000);assert u.reg_read(UC_X86_REG_EIP)==0x200100
  assert bytes(u.mem_read(target+5,n-5))==b'\xcc'*(n-5)
  assert all(e['pc'] in [0x100fbcdd,0x100fbcf3] and e['size'] in [1,4] for e in fill_events)
  writes.extend(fill_events)

# Eight observed font registrations extend the renderer bank before these
# seven unchanged audio registrations. Compile their derived locations now;
# the live publication still compares every instruction byte and callback.
renderer_path=R/'generated/integration-review/renderer-hook-bank/plan.json'
renderer=json.loads(renderer_path.read_text());assert renderer['verified'] and len(renderer['trampolines'])==26
assert sorted(r['address'] for r in renderer['trampolines'])==[0x3c0ea0+72*i for i in range(26)]
new_start=max(r['address'] for r in renderer['trampolines'])+72
old_start=min(r['address'] for r in records);delta=new_start-old_start;assert delta==8*72
expected_bank=bytearray(b'\xcc'*65536)
for row in records:
 old_address=row['address'];code=bytearray.fromhex(row['code_hex']);instructions=list(md.disasm(code,old_address))
 assert instructions[-1].mnemonic=='jmp' and instructions[-1].size==5 and instructions[-1].operands[0].imm==row['continuation']
 assert not any(i.group(capstone.CS_GRP_JUMP) or i.group(capstone.CS_GRP_CALL) for i in instructions[:-1])
 row['address']+=delta
 code[-4:]=((row['continuation']-row['address']-len(code))&0xffffffff).to_bytes(4,'little')
 assert list(md.disasm(code,row['address']))[-1].operands[0].imm==row['continuation']
 row['code_hex']=code.hex();row['captured_address']=old_address
 # Heap pending-record pointers are not assumed to survive changed startup.
 row.pop('pending_record')
 expected_bank[row['address']-0x3c0000:row['address']-0x3c0000+len(code)]=code
captured_bank=bank;bank=out/'expected-font-audio-bank.bin';bank.write_bytes(expected_bank)
assert len({r['original_target'] for r in records})==7
plan=dict(verified=True,native_activation=False,trampolines=records,code_writes=writes,bank_path=str(bank.relative_to(R)),bank_sha256=sha(bank),image_sha256=sha(image_path),capture_sha256=sha(capture/'pending-hooks.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),relocation=dict(delta=delta,renderer_plan_sha256=sha(renderer_path),captured_bank_sha256=sha(captured_bank)),scope='Original seven captured audio prologues with derived addresses after the26 observed renderer registrations. Live publication must match exact derived bytes and callbacks before activation. Original relocated instructions and branch targets checked. Small fills independently execute the pinned original x86 memset to recover exact writer PCs, widths and order. Not yet published or integrated.')
(out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n');print('Validated7 pending hooks and',len(writes),'ordered write records')
