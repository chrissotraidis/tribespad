#!/usr/bin/env python3
"""Audit and strictly lift the observed HTTP initialization patch, without publication.

The trace reaches the enclosing routine return. This compiler candidate is
not a guest-loadable image or evidence of native publication.
"""
import argparse,hashlib,json,os,pathlib,re,shutil,struct,subprocess
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--prepare-native',action='store_true');a=p.parse_args()
tp=R/'generated/integration-review/curl-version-checkpoint-20261002/reference/result.json';trace=json.loads(tp.read_text())
assert trace['completed_patch_routine'] and not trace['unconfirmed_writes']
assert trace['stop']['kind']=='patch_routine_return' and trace['stop']['pc']==0x10061031
assert trace['script_sha256']==sha(R/'scripts/trace-http-qol-patches.py')
cp=R/'generated/integration-review/curl-version-checkpoint-20261002/native/result.json';assert sha(cp)==trace['checkpoint_sha256'];native=json.loads(cp.read_text());assert len(native['network_code_writes'])==11
inputs=trace['checkpoint_inputs'];assert inputs==native['http_patch_checkpoint']
frozen=cp.parent
for name,row in inputs.items():assert sha(frozen/name)==row['sha256']
pages=json.loads((frozen/'http-patch-pages.json').read_text())['ranges']
initial={row['address']:(frozen/row['file']).read_bytes() for row in pages};working={k:bytearray(v) for k,v in initial.items()}
def locate(address,size):
 rows=[r for r in pages if r['address']<=address and address+size<=r['address']+r['size']];assert len(rows)==1,(hex(address),size)
 return rows[0]['address'],address-rows[0]['address']
def read(data,address,size):
 result=bytearray()
 while size:
  row=next(r for r in pages if r['address']<=address<r['address']+r['size']);n=min(size,row['address']+row['size']-address);result+=data[row['address']][address-row['address']:address-row['address']+n];address+=n;size-=n
 return bytes(result)
for event in trace['code_writes']+trace['image_data_writes']:
 assert event['completed'];base,offset=locate(event['address'],event['size']);before=bytes.fromhex(event['before']);after=bytes.fromhex(event['after']);assert len(before)==len(after)==event['size']
 assert bytes(working[base][offset:offset+len(before)])==before,(hex(event['address']),'write preimage')
 working[base][offset:offset+len(after)]=after
# Every engine, DLL and bank byte change is accounted for, including bank
# allocator metadata. Stack/heap/TEB writes are outside this prefix plan.
for row in pages:
 if not (0x400000<=row['address']<0xa00000 or 0x10000000<=row['address']<0x10500000 or row['address']==0x3c0000):continue
 change=next((r for r in trace['changed_regions'] if r['address']==row['address']),None)
 final=initial[row['address']]
 if change:
  path=tp.parent/pathlib.Path(change['after_path']).name;assert sha(path)==change['after_sha256'];final=path.read_bytes()
 assert working[row['address']]==final,('Unrecorded mutation',hex(row['address']))
source=R/'generated/integration-review/network-patched-engine';parent=json.loads((source/'result.json').read_text());assert parent['state']=='strictly_lifted_unpublished' and not parent['guest_loadable'];assert sha(source/'Tribes2.exe')==parent['image_sha256']
raw=bytearray((source/'Tribes2.exe').read_bytes());pe=pefile.PE(data=bytes(raw));base=pe.OPTIONAL_HEADER.ImageBase
# The parent is the already verified cumulative fifth-phase compiler candidate.
prior=[]
def patch(row):
 before=bytes.fromhex(row['before']);after=bytes.fromhex(row['after']);off=pe.get_offset_from_rva(row['address']-base);assert raw[off:off+len(before)]==before,(hex(row['address']),'compiler preimage');raw[off:off+len(after)]=after
for row in prior:patch(row)
for section in pe.sections:
 if section.Characteristics&0x20000000:
  start=base+section.VirtualAddress;size=section.SizeOfRawData;assert bytes(raw[section.PointerToRawData:section.PointerToRawData+size])==read(initial,start,size),('Cumulative code mismatch',hex(start))
engine=[e for e in trace['code_writes'] if e['image']=='engine'];assert len(engine)==17
for row in engine:patch(row)
for section in pe.sections:
 if section.Characteristics&0x20000000:
  start=base+section.VirtualAddress;size=section.SizeOfRawData;assert bytes(raw[section.PointerToRawData:section.PointerToRawData+size])==read(working,start,size)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;calls=[]
bank_plan_path=R/'generated/integration-review/curl-version-checkpoint-20261002/http-patch-audit.json';bank_plan=json.loads(bank_plan_path.read_text());assert bank_plan['verified'] and bank_plan['trace_sha256']==sha(tp)
assert bank_plan['script_sha256']==sha(R/'scripts/audit-http-qol-patches.py')
assert bank_plan['engine_writes']==engine
for edit in bank_plan['instruction_edits']:
 if not edit['new'].startswith('call '):continue
 pc=edit['pc'];old=next(md.disasm(read(initial,pc,5),pc));new=next(md.disasm(read(working,pc,5),pc))
 assert old.mnemonic==new.mnemonic=='call';calls.append(dict(call_site=pc,old_target=old.operands[0].imm,new_target=new.operands[0].imm))
# This CALL exists only in the replacement block, then receives its final
# displacement in a later store. Decode the fully replayed bytes.
new=next(md.disasm(read(working,0x44aca4,5),0x44aca4));assert new.mnemonic=='call' and new.operands[0].imm==0x100238c0
calls.append(dict(call_site=0x44aca4,old_target=None,new_target=new.operands[0].imm))
out=R/'generated/integration-review/http-patched-engine';out.mkdir(exist_ok=True);(out/'result.json').write_text('{"state":"preparing","published":false}\n')
# Retarget true absolute fixups; remove audited overwritten operands. Relative branches
# are decoded from bytes and are never invented as relocation entries.
relocations=[];adjustments=[];removed=set()
for line in (source/'relocations.csv').read_text().splitlines():
 address,target=[int(x,0) for x in line.split(',')[:2]]
 if any(address<r['address']+len(bytes.fromhex(r['after'])) and address+4>r['address'] for r in prior+engine):
  offset=pe.get_offset_from_rva(address-base);after=bytes(raw[offset:offset+4])
  if address==bank_plan['block_replacement']['removed_absolute_fixup']:
   assert address==0x44aca8 and target==0x757918 and bytes.fromhex(next(e['before'] for e in engine if e['address']==address))[:4]==struct.pack('<I',target)
   removed.add(address);adjustments.append(dict(address=address,old_target=target,action='removed_from_replaced_instruction'));continue
  if after==b'\x90'*4:removed.add(address);adjustments.append(dict(address=address,old_target=target,action='removed_from_NOP_fill'));continue
  newtarget=int.from_bytes(after,'little');assert any(base+s.VirtualAddress<=newtarget<base+s.VirtualAddress+max(s.SizeOfRawData,s.Misc_VirtualSize) for s in pe.sections),(hex(address),hex(newtarget))
  adjustments.append(dict(address=address,old_target=target,new_target=newtarget,action='updated_absolute_pointer'));target=newtarget
 relocations.append('0x%x,0x%x,\n'%(address,target))
disabled=set()
for block in pe.DIRECTORY_ENTRY_BASERELOC:
 for entry in block.entries:
  if base+entry.rva in removed:assert entry.type==3;struct.pack_into('<H',raw,entry.struct.get_file_offset(),entry.rva&0xfff);disabled.add(base+entry.rva)
assert disabled==removed
(out/'Tribes2.exe').write_bytes(raw);(out/'relocations.csv').write_text(''.join(relocations))
for path in source.glob('*.sci'):shutil.copy2(path,out/path.name)
if (source/'literal_fixups.csv').exists():shutil.copy2(source/'literal_fixups.csv',out/'literal_fixups.csv')
targets=sorted(set(parent['external_targets'])|{r['new_target'] for r in calls if r['new_target']>=0x10000000})
(out/'external_procedures.sci').write_text(''.join('loc_%X,loc_%X\n'%(v,v) for v in targets))
old_entries=sorted(int(x,16) for x in re.findall(r'^proc loc_([0-9A-F]+)\b',(source/'seg01_code.llinc').read_text(),re.M));keep=set(parent['continuations'])
for row in prior+engine:
 begin,end=row['address'],row['address']+len(bytes.fromhex(row['after']));keep.update(x for x in old_entries if begin<=x<end);keep.add(max(x for x in old_entries if x<=begin));keep.add(next(x for x in old_entries if x>=end))
# Retain the new CALL return, its direct jump destination and the new
# trampoline continuation. The old CALL tail is unreachable fallthrough.
keep.update([0x44aca9,0x44ad48])
keep.update(r['continuation'] for r in bank_plan['trampolines'])
(out/'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n'%(v,v) for v in sorted(keep)))
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];env={k:v for k,v in os.environ.items() if k!='HALOPAD_SRW_DIAG'};env['TRIBES_SRW_FULL_FLAGS']='1'
with (out/'srw.log').open('w') as log:run=subprocess.run([str(srw),'Tribes2.exe','tribes2.llasm'],cwd=out,env=env,stdout=log,stderr=log,timeout=600)
assert run.returncode==0,out/'srw.log'
code=(out/'seg01_code.llinc').read_text();assert 'halopad_trap_unimplemented' not in code
for row in calls:assert 'PUSH loc_%X\ntcall loc_%X\n'%(row['call_site']+5,row['new_target']) in code,row
assert all(re.search(r'^proc loc_%X\b'%v,code,re.M) for v in keep)
main=(out/'tribes2.llasm').read_text();includes=''.join('include %s\n'%n for n in ['tribes_mmx.llinc','tribes_sse.llinc','tribes_scan.llinc','tribes_x87.llinc','tribes_shift.llinc','tribes_atomic.llinc']);(out/'tribes2.llasm').write_text(includes+main)
known=set(re.findall(r'^proc (\S+) external',main,re.M));imports={i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
(out/'extern.llinc').write_text(''.join('proc %s external\n'%n for n in sorted(imports-known))+''.join('proc loc_%X external\n'%v for v in targets));(out/'macros.llinc').write_text('')
report=dict(state='strictly_lifted_unpublished',exit=0,diagnostic=False,full_flags=True,image_sha256=sha(out/'Tribes2.exe'),original_image_sha256=parent['original_image_sha256'],srw_sha256=sha(srw),patch_candidate=True,guest_loadable=False,published=False,complete_phase=True,parent_build_sha256=sha(source/'result.json'),trace_path=str(tp.relative_to(R)),trace_sha256=sha(tp),checkpoint_sha256=sha(cp),external_targets=targets,continuations=sorted(keep),relocation_adjustments=adjustments,code_calls=calls,engine_writes=engine,bank_plan_sha256=sha(bank_plan_path),bank_write_count=sum(e['image']=='bank' for e in trace['code_writes']),script_sha256=sha(pathlib.Path(__file__).resolve()),code_sha256=sha(out/'seg01_code.llinc'),scope='Compiler-only completed HTTP initialization patch. All recorded image/bank writes replay and every cumulative executable byte matches checkpoint/trace. One audited obsolete absolute fixup is removed; original entry routes are retained for differential tests. No bank publication, native activation or gameplay claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
if a.prepare_native:subprocess.run([str(R/'.venv/bin/python'),str(R/'scripts/prepare-va-model.py'),str(out)],check=True)
print('Strict unpublished HTTP initialization patch:',len(engine),'engine writes,',len(calls),'calls,',len(keep),'retained entries')
