#!/usr/bin/env python3
"""Validate later patch trace against native preimages and immutable publication."""
import hashlib,json,pathlib,struct
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
trace_path=R/'generated/integration-review/late-patch-trace/result.json';trace=json.loads(trace_path.read_text());assert trace['completed_patch_routine'] and not trace['unconfirmed_writes']
assert sha(R/'scripts/trace-late-qol-patches.py')==trace['script_sha256']
checkpoint=R/trace['checkpoint_path'];assert sha(checkpoint)==trace['checkpoint_sha256'];native=json.loads(checkpoint.read_text());assert native['initialized'] and native['native_patch_publication']
meta=trace['checkpoint_inputs'];assert meta==native['late_patch_checkpoint']
for row in meta.values():assert sha(R/row['path'])==row['sha256']
pages=json.loads((R/meta['late-pages.json']['path']).read_text())['ranges'];initial={row['address']:bytearray((R/meta[row['file']]['path']).read_bytes()) for row in pages}
def locate(address,size):
 rows=[row for row in pages if row['address']<=address and address+size<=row['address']+row['size']];assert len(rows)==1,(hex(address),size);return rows[0],address-rows[0]['address']
def read(address,size):
 data=bytearray()
 while size:
  row=next(r for r in pages if r['address']<=address<r['address']+r['size']);n=min(size,row['address']+row['size']-address);data+=initial[row['address']][address-row['address']:address-row['address']+n];address+=n;size-=n
 return bytes(data)
manifest_path=checkpoint.parent/'published-code.bin';manifest=manifest_path.read_bytes();count=struct.unpack_from('<I',manifest)[0];offset=4
for i in range(count):
 address,size=struct.unpack_from('<II',manifest,offset);offset+=8;assert read(address,size)==manifest[offset:offset+size],('Published preimage changed',hex(address));offset+=size
assert offset==len(manifest)
working={a:bytearray(b) for a,b in initial.items()};events=trace['code_writes'];assert all(e['image']=='engine' for e in events),'Unexpected late code owner'
engine_data=[e for e in trace['image_data_writes'] if e['image']=='engine'];assert len(engine_data)==1 and engine_data[0]['address']==0x799b24 and engine_data[0]['size']==4
engine=pefile.PE(str(R/'ref/control/Tribes2.exe'));base=engine.OPTIONAL_HEADER.ImageBase
for e in events+engine_data:
 assert e['completed'];row,offset=locate(e['address'],e['size']);before=bytes.fromhex(e['before']);after=bytes.fromhex(e['after']);assert len(before)==len(after)==e['size'];data=working[row['address']]
 assert bytes(data[offset:offset+e['size']])==before;data[offset:offset+e['size']]=after
 # All late updates must remain inside the original file-backed image.
 rva=e['address']-base;assert any(s.VirtualAddress<=rva and rva+e['size']<=s.VirtualAddress+s.SizeOfRawData for s in engine.sections)
for row in pages:
 if not (base<=row['address']<base+engine.OPTIONAL_HEADER.SizeOfImage):continue
 changes=[r for r in trace['changed_regions'] if r['address']==row['address']]
 after=initial[row['address']]
 if changes:
  path=R/changes[0]['after_path'];assert sha(path)==changes[0]['after_sha256'];after=path.read_bytes()
 assert working[row['address']]==after,'Unaccounted engine mutation'
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
calls=[]
for e in events:
 if e['size']==4 and read(e['address']-1,1)==b'\xe8':
  before=b'\xe8'+bytes.fromhex(e['before']);after=b'\xe8'+bytes.fromhex(e['after']);old=list(md.disasm(before,e['address']-1));new=list(md.disasm(after,e['address']-1));assert len(old)==len(new)==1 and old[0].mnemonic==new[0].mnemonic=='call'
  calls.append(dict(call_site=e['address']-1,old_target=old[0].operands[0].imm,new_target=new[0].operands[0].imm,before_hex=before.hex(),after_hex=after.hex(),writer_pc=e['pc']))
assert len(calls)==4
spans=[]
for address in sorted({a for e in events+engine_data for a in range(e['address'],e['address']+e['size'])}):
 if spans and address==spans[-1][1]:spans[-1][1]+=1
 else:spans.append([address,address+1])
regions=[]
for begin,end in spans:
 row,offset=locate(begin,end-begin);regions.append(dict(address=begin,size=end-begin,before_hex=read(begin,end-begin).hex(),after_hex=bytes(working[row['address']][offset:offset+end-begin]).hex(),category='data_pointer' if begin==0x799b24 else 'code'))
flushes=[e for e in trace['services'] if e['name']=='FlushInstructionCache'];assert len(flushes)==1 and flushes[0]['effect']['address']==0x3c0000 and flushes[0]['effect']['bytes']==65536
plan=dict(verified=True,native_activated=False,trace_path=str(trace_path.relative_to(R)),trace_sha256=sha(trace_path),checkpoint_path=str(checkpoint.relative_to(R)),checkpoint_sha256=sha(checkpoint),publication_manifest_sha256=sha(manifest_path),initial_plan_sha256=sha(R/'generated/integration-review/native-patch-current.json'),code_writes=events,engine_data_writes=engine_data,engine_regions=regions,code_calls=calls,data_targets=[int.from_bytes(bytes.fromhex(e['after']),'little') for e in engine_data],final_flush=flushes[0],script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Verified ordered late code writes and complete engine byte differences from actual native pre-write state through bounded original x86 patch-routine return. Later engine target bodies and native staged activation remain unverified.')
out=R/'generated/integration-review/late-patch-plan.json';out.write_text(json.dumps(plan,indent=2)+'\n');print(json.dumps({'code_writes':len(events),'regions':len(regions),'code_calls':calls,'data_targets':plan['data_targets']},indent=2))
