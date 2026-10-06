#!/usr/bin/env python3
"""Audit the completed reference suffix and five newly registered bank records."""
import argparse,hashlib,json,pathlib,struct
import capstone
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('trace',type=pathlib.Path);ap.add_argument('--output',type=pathlib.Path,required=True);a=ap.parse_args();tp=a.trace.resolve();trace=json.loads(tp.read_text());assert trace['completed_patch_routine'] and trace['stop']['pc']==0x1006102c and not trace['unconfirmed_writes'];assert trace['script_sha256']==sha(R/'scripts/trace-network-qol-patches.py')
cp=R/trace['checkpoint_path'];assert sha(cp)==trace['checkpoint_sha256'];capture=json.loads(cp.read_text());inputs=capture['network_patch_checkpoint'];manifest=json.loads((R/inputs['network-patch-pages.json']['path']).read_text());pages=manifest['ranges'];before={}
for row in pages:
 item=inputs[row['file']];p=R/item['path'];assert sha(p)==item['sha256'];before[row['address']]=p.read_bytes()
after={k:bytearray(v) for k,v in before.items()}
def region(address,size):return next(r for r in pages if r['address']<=address and address+size<=r['address']+r['size'])
def read(table,address,size):
 r=region(address,size);off=address-r['address'];return bytes(table[r['address']][off:off+size])
for e in trace['code_writes']+trace['image_data_writes']:
 # Code and image-data ranges are disjoint, so within-category order suffices.
 assert e['completed'] and read(after,e['address'],e['size']).hex()==e['before']
 r=region(e['address'],e['size']);off=e['address']-r['address'];after[r['address']][off:off+e['size']]=bytes.fromhex(e['after'])
for row in trace['changed_regions']:
 if row['address'] in (0x3c0000,) or 0x400000<=row['address']<0x800000 or 0x10000000<=row['address']<0x10500000:
  path=R/row['after_path'];assert sha(path)==row['after_sha256'];assert bytes(after[row['address']])==path.read_bytes(),hex(row['address'])
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;blocks=[]
for e in trace['code_writes']:
 if e['image']!='engine':continue
 assert e['size']==4;pc=e['address']-1;old=next(md.disasm(read(before,pc,5),pc));new=next(md.disasm(read(after,pc,5),pc));assert old.size==new.size==5
 if pc==0x59fdd3:assert old.mnemonic==new.mnemonic=='push' and old.operands[0].imm==0x374 and new.operands[0].imm==0x394
 else:assert old.mnemonic==new.mnemonic=='call' and 0x10000000<=new.operands[0].imm<0x10129000
 blocks.append(dict(pc=pc,old=old.mnemonic+' '+old.op_str,new=new.mnemonic+' '+new.op_str,writer=e['pc']))
assert len(blocks)==11
bank=0x3c0000;native_end=struct.unpack('<I',read(before,bank+8,4))[0];end=struct.unpack('<I',read(after,bank+8,4))[0];assert native_end==bank+0x870 and end==bank+0x8b8
oldp=R/'generated/integration-review/native-patch-current.json';old=json.loads(oldp.read_text());prevp=R/'generated/integration-review/followup-bank-plan.json';prev=json.loads(prevp.read_text());assert old['verified'] and prev['verified']
for record in old['trampolines']+prev['trampolines']:
 code=bytes.fromhex(record['code_hex']);assert read(before,record['address'],len(code))==code==read(after,record['address'],len(code))
tramps=[]
for address in range(bank+0x750,end,72):
 block=read(after,address,72);moved,restored=block[30],block[54];assert 0<moved==restored<=22
 continuation,detour=struct.unpack_from('<II',block,64);target=continuation-restored;assert 0x400000<=target<0x700000 and 0x10000000<=detour<0x10129000
 saved=read(before,target,restored);assert block[32:32+restored]==saved
 code=block[:moved+5];ins=list(md.disasm(code,address));original=list(md.disasm(saved,target));assert sum(i.size for i in ins)==len(code) and len(ins)==len(original)+1
 assert ins[-1].mnemonic=='jmp' and ins[-1].operands[0].imm==continuation
 for x,y in zip(original,ins[:-1]):assert not x.group(capstone.CS_GRP_JUMP) and not x.group(capstone.CS_GRP_CALL) and bytes(x.bytes)==bytes(y.bytes)
 tramps.append(dict(address=address,original_target=target,continuation=continuation,detour_target=detour,code_hex=code.hex(),restore_hex=saved.hex(),observed_in_native_checkpoint=address<native_end))
report=dict(verified=True,native_activation=False,trace_path=str(tp.relative_to(R)),trace_sha256=sha(tp),checkpoint_sha256=sha(cp),script_sha256=sha(pathlib.Path(__file__).resolve()),prior_plans_sha256={str(p.relative_to(R)):sha(p) for p in [oldp,prevp]},engine_instructions=blocks,trampolines=tramps,scope='Ten call redirects and one allocation-size immediate; five new registered prologues, four already observed natively and one in the reference suffix. All21 prior bank instruction intervals unchanged. Full strict compilation, continuation routing and native publication remain required.')
a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS: 11 complete instruction edits and 5 new prologue records; 21 prior bank prefixes unchanged')
