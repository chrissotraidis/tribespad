#!/usr/bin/env python3
"""Verify six observed new hook records against their live original prologues."""
import hashlib,json,pathlib,struct
import capstone
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
tp=R/'generated/integration-review/followup-patch-trace/result.json';trace=json.loads(tp.read_text());assert trace['script_sha256']==sha(R/'scripts/trace-followup-qol-patches.py') and not trace['completed_patch_routine'];assert trace['stop']['name']=='GetAdaptersAddresses'
cp=R/trace['checkpoint_path'];assert sha(cp)==trace['checkpoint_sha256'];capture=json.loads(cp.read_text());inputs=capture['followup_patch_checkpoint'];pages=json.loads((R/inputs['followup-patch-pages.json']['path']).read_text())['ranges']
initial={}
for row in pages:
 item=inputs[row['file']];path=R/item['path'];assert sha(path)==item['sha256'];initial[row['address']]=path.read_bytes()
def read(address,size):
 row=next(r for r in pages if r['address']<=address and address+size<=r['address']+r['size']);return initial[row['address']][address-row['address']:address-row['address']+size]
bank=0x3c0000;change=next(r for r in trace['changed_regions'] if r['address']==bank);path=R/change['after_path'];assert sha(path)==change['after_sha256'];data=path.read_bytes();assert len(data)==65536 and data[:4]==b'dtrR'
oldpath=R/'generated/integration-review/native-patch-current.json';old=json.loads(oldpath.read_text());assert old['verified']
# Existing published instruction bytes cannot be modified by a new allocation.
for row in old['trampolines']:
 begin=row['address'];code=bytes.fromhex(row['code_hex']);assert read(begin,len(code))==code and data[begin-bank:begin-bank+len(code)]==code
 for e in trace['code_writes']:
  if e['image']=='bank':assert e['address']+e['size']<=begin or e['address']>=begin+len(code)
start=struct.unpack_from('<I',initial[bank],8)[0];end=struct.unpack_from('<I',data,8)[0];assert start==bank+0x5a0 and end==bank+0x750 and (end-start)//72==6
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;tramps=[]
for address in range(start,end,72):
 block=data[address-bank:address-bank+72];moved,restored=block[30],block[54];assert 0<moved==restored<=22
 continuation,detour=struct.unpack_from('<II',block,64);target=continuation-restored;assert 0x400000<=target<0x700000 and 0x10000000<=detour<0x10129000
 saved=read(target,restored);assert block[32:32+restored]==saved
 code=block[:moved+5];ins=list(md.disasm(code,address));original=list(md.disasm(saved,target));assert sum(i.size for i in ins)==len(code) and len(ins)==len(original)+1
 assert ins[-1].mnemonic=='jmp' and ins[-1].operands[0].imm==continuation
 for before,after in zip(original,ins[:-1]):
  assert not before.group(capstone.CS_GRP_JUMP) and not before.group(capstone.CS_GRP_CALL) and bytes(before.bytes)==bytes(after.bytes)
 tramps.append(dict(address=address,original_target=target,original_image='engine',continuation=continuation,detour_target=detour,moved_code_bytes=moved,code_hex=code.hex(),restore_hex=saved.hex()))
report=dict(verified=True,native_patch_publication=False,complete_phase=False,trace_path=str(tp.relative_to(R)),trace_sha256=sha(tp),checkpoint_sha256=sha(cp),previous_plan_sha256=sha(oldpath),native_allocation=old['native_allocation'],bank_path=str(path.relative_to(R)),bank_sha256=sha(path),trampolines=tramps,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Six new records reproduce live original engine prologues and exact continuation branches; all previously published bank instructions remain untouched. Registration only: engine detours, continuations, phase completion and native activation remain unverified.')
output=R/'generated/integration-review/followup-bank-plan.json';output.write_text(json.dumps(report,indent=2)+'\n');print('Verified six new records; all',len(old['trampolines']),'published trampoline instruction intervals unchanged')
