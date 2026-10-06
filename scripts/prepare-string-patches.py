#!/usr/bin/env python3
"""Audit and strictly lift the observed string/utility patch, without publication.

The trace reaches the enclosing routine return. This compiler candidate is
not a guest-loadable image or evidence of native publication.
"""
import argparse,bisect,hashlib,json,os,pathlib,re,shutil,struct,subprocess
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--prepare-native',action='store_true');a=p.parse_args()
tp=R/'generated/integration-review/http-publication-checkpoint-20261003/next-reference/result.json';trace=json.loads(tp.read_text())
assert trace['completed_patch_routine'] and not trace['unconfirmed_writes']
assert trace['stop']['kind']=='patch_routine_return' and trace['stop']['pc']==0x10061036
assert trace['script_sha256']==sha(R/'scripts/trace-string-qol-patches.py')
cp=R/'generated/integration-review/http-publication-checkpoint-20261003/native-proof/result.json';assert sha(cp)==trace['checkpoint_sha256'];native=json.loads(cp.read_text());assert len(native['http_code_writes'])==17
inputs=trace['checkpoint_inputs'];assert inputs==native['http_patch_final']
frozen=cp.parent
for name,row in inputs.items():assert sha(frozen/name)==row['sha256']
pages=json.loads((frozen/'http-final-pages.json').read_text())['ranges']
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
source=R/'generated/integration-review/http-patched-engine';parent=json.loads((source/'result.json').read_text());assert parent['state']=='strictly_lifted_unpublished' and not parent['guest_loadable'];assert sha(source/'Tribes2.exe')==parent['image_sha256']
raw=bytearray((source/'Tribes2.exe').read_bytes());pe=pefile.PE(data=bytes(raw));base=pe.OPTIONAL_HEADER.ImageBase
# The parent is the already verified cumulative sixth-phase compiler candidate.
prior=[]
def patch(row):
 before=bytes.fromhex(row['before']);after=bytes.fromhex(row['after']);off=pe.get_offset_from_rva(row['address']-base);assert raw[off:off+len(before)]==before,(hex(row['address']),'compiler preimage');raw[off:off+len(after)]=after
for row in prior:patch(row)
for section in pe.sections:
 if section.Characteristics&0x20000000:
  start=base+section.VirtualAddress;size=section.SizeOfRawData;assert bytes(raw[section.PointerToRawData:section.PointerToRawData+size])==read(initial,start,size),('Cumulative code mismatch',hex(start))
engine=[e for e in trace['code_writes'] if e['image']=='engine'];assert len(engine)==114
for row in engine:patch(row)
for section in pe.sections:
 if section.Characteristics&0x20000000:
  start=base+section.VirtualAddress;size=section.SizeOfRawData;assert bytes(raw[section.PointerToRawData:section.PointerToRawData+size])==read(working,start,size)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True;calls=[]
bank_plan_path=R/'generated/integration-review/http-publication-checkpoint-20261003/next-patch-audit.json';bank_plan=json.loads(bank_plan_path.read_text());assert bank_plan['verified'] and bank_plan['trace_sha256']==sha(tp)
assert bank_plan['script_sha256']==sha(R/'scripts/audit-string-qol-patches.py')
assert bank_plan['engine_writes']==engine
old_entries=sorted(int(x,16) for x in re.findall(r'^proc loc_([0-9A-F]+)\b',(source/'seg01_code.llinc').read_text(),re.M))
for row in engine:
 if row['size']!=4 or read(initial,row['address']-1,1)!=b'\xe8':continue
 pc=row['address']-1;entry=old_entries[bisect.bisect_right(old_entries,pc)-1]
 if pc not in {i.address for i in md.disasm(read(initial,entry,pc-entry+5),entry)}:continue
 old=next(md.disasm(read(initial,pc,5),pc));new=next(md.disasm(read(working,pc,5),pc))
 assert old.mnemonic==new.mnemonic=='call';calls.append(dict(call_site=pc,old_target=old.operands[0].imm,new_target=new.operands[0].imm))
# These replacement spans have independently inspected instruction boundaries.
# Their new operands are real guest addresses, never native host pointers.
new_fixups={0x44b7c1:0x9e4ae8,0x689d19:0x79cad4}
for pc,operand,expected,size in [(0x44b7bf,0x44b7c1,0x9e4ae8,6),(0x689d17,0x689d19,0x79cad4,6)]:
 ins=next(md.disasm(read(working,pc,size),pc));assert ins.size==size
 assert int.from_bytes(read(working,operand,4),'little')==expected
 if ins.mnemonic=='call':assert ins.operands[0].type==capstone.x86.X86_OP_MEM and ins.operands[0].mem.disp==expected and pc+ins.disp_offset==operand
 else:assert ins.mnemonic=='cmp' and ins.operands[1].imm==expected and pc+ins.imm_offset==operand
out=R/'generated/integration-review/string-patched-engine';out.mkdir(exist_ok=True);(out/'result.json').write_text('{"state":"preparing","published":false}\n')
# Retarget true absolute fixups; remove audited overwritten operands. Relative branches
# are decoded from bytes and are never invented as relocation entries.
relocations=[];adjustments=[];removed=set()
for line in (source/'relocations.csv').read_text().splitlines():
 address,target=[int(x,0) for x in line.split(',')[:2]]
 if any(address<r['address']+len(bytes.fromhex(r['after'])) and address+4>r['address'] for r in prior+engine):
  offset=pe.get_offset_from_rva(address-base);after=bytes(raw[offset:offset+4])
  if address==0x5c4810:
   assert target==0x83f360 and after==bytes.fromhex('eb3f9090')
   removed.add(address);adjustments.append(dict(address=address,old_target=target,action='removed_from_replaced_instruction'));continue
  if address==0x42757f:
   assert target==0x427450 and after==struct.pack('<I',0x10011480)
   assert 'define hidden fastcc void @loc_10011480(' in (R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/module.ll').read_text()
   removed.add(address);adjustments.append(dict(address=address,old_target=target,new_target=0x10011480,action='fixed_guest_pointer_into_qol'));continue
  if after==b'\x90'*4:removed.add(address);adjustments.append(dict(address=address,old_target=target,action='removed_from_NOP_fill'));continue
  newtarget=int.from_bytes(after,'little');assert any(base+s.VirtualAddress<=newtarget<base+s.VirtualAddress+max(s.SizeOfRawData,s.Misc_VirtualSize) for s in pe.sections),(hex(address),hex(newtarget))
  adjustments.append(dict(address=address,old_target=target,new_target=newtarget,action='updated_absolute_pointer'));target=newtarget
 relocations.append('0x%x,0x%x,\n'%(address,target))
disabled=set()
for block in pe.DIRECTORY_ENTRY_BASERELOC:
 for entry in block.entries:
  if base+entry.rva in removed:assert entry.type==3;struct.pack_into('<H',raw,entry.struct.get_file_offset(),entry.rva&0xfff);disabled.add(base+entry.rva)
assert disabled==removed
# Add the two new absolute operands to the compiler-only PE relocation
# directory. Existing raw-section padding holds separate legal page blocks;
# executable bytes remain exactly those captured from the original patch.
assert not set(new_fixups)&{int(line.split(',')[0],0) for line in relocations}
directory=pe.OPTIONAL_HEADER.DATA_DIRECTORY[5];section=pe.get_section_by_rva(directory.VirtualAddress)
start=pe.get_offset_from_rva(directory.VirtualAddress)+directory.Size
for index,(address,target) in enumerate(sorted(new_fixups.items())):
 offset=start+12*index;assert offset+12<=section.PointerToRawData+section.SizeOfRawData
 assert raw[offset:offset+12]==bytes(12)
 struct.pack_into('<IIHH',raw,offset,(address-base)&~4095,12,0x3000|((address-base)&4095),0)
 relocations.append('0x%x,0x%x,\n'%(address,target));adjustments.append(dict(address=address,new_target=target,action='added_absolute_operand'))
struct.pack_into('<I',raw,directory.get_file_offset()+4,directory.Size+12*len(new_fixups))
(out/'Tribes2.exe').write_bytes(raw);(out/'relocations.csv').write_text(''.join(relocations))
for path in source.glob('*.sci'):shutil.copy2(path,out/path.name)
if (source/'literal_fixups.csv').exists():shutil.copy2(source/'literal_fixups.csv',out/'literal_fixups.csv')
targets=sorted(set(parent['external_targets'])|{r['new_target'] for r in calls if r['new_target']>=0x10000000})
(out/'external_procedures.sci').write_text(''.join('loc_%X,loc_%X\n'%(v,v) for v in targets))
keep=set(parent['continuations'])
for row in prior+engine:
 begin,end=row['address'],row['address']+len(bytes.fromhex(row['after']));keep.update(x for x in old_entries if begin<=x<end);keep.add(max(x for x in old_entries if x<=begin));keep.add(next(x for x in old_entries if x>=end))
# Retain the new CALL return, its direct jump destination and the new
# trampoline continuation. The old CALL tail is unreachable fallthrough.
keep.update([0x4200c3,0x4200c9,0x44b7c5,0x689d0f,0x689d11,0x689d1f])
keep.update(r['continuation'] for r in bank_plan['trampolines'])
# The former branch entry44b7c0 now falls inside the new indirect CALL.
# SRW's single instruction map cannot decode both streams together. Retain
# the historical entry through a separately strict-lifted exact byte stream.
overlap_entries=[dict(address=0x44b7c0,code_hex=read(working,0x44b7c0,7).hex(),targets=[0x44b7e8]),dict(address=0x44b7c7,code_hex=read(working,0x44b7c7,27).hex(),targets=[0x44b7e2,0x44b7ed])]
assert overlap_entries[0]['code_hex']=='15e84a9e00eb21'
for row in overlap_entries:assert row['address'] in old_entries;keep.discard(row['address'])
keep.update([0x44b7e2,0x44b7ed])
(out/'overlap-entries.json').write_text(json.dumps(overlap_entries,indent=2)+'\n')
(out/'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n'%(v,v) for v in sorted(keep)))
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];env={k:v for k,v in os.environ.items() if k!='HALOPAD_SRW_DIAG'};env['TRIBES_SRW_FULL_FLAGS']='1'
with (out/'srw.log').open('w') as log:run=subprocess.run([str(srw),'Tribes2.exe','tribes2.llasm'],cwd=out,env=env,stdout=log,stderr=log,timeout=600)
assert run.returncode==0,out/'srw.log'
code=(out/'seg01_code.llinc').read_text();assert 'halopad_trap_unimplemented' not in code
for row in calls:assert 'PUSH loc_%X\ntcall loc_%X\n'%(row['call_site']+5,row['new_target']) in code,row
emitted=set(int(v,16) for v in re.findall(r'^proc loc_([0-9A-F]+)\b',code,re.M));assert keep<=emitted,sorted(keep-emitted)
main=(out/'tribes2.llasm').read_text();includes=''.join('include %s\n'%n for n in ['tribes_mmx.llinc','tribes_sse.llinc','tribes_scan.llinc','tribes_x87.llinc','tribes_shift.llinc','tribes_atomic.llinc']);(out/'tribes2.llasm').write_text(includes+main)
known=set(re.findall(r'^proc (\S+) external',main,re.M));imports={i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
(out/'extern.llinc').write_text(''.join('proc %s external\n'%n for n in sorted(imports-known))+''.join('proc loc_%X external\n'%v for v in targets));(out/'macros.llinc').write_text('')
report=dict(state='strictly_lifted_unpublished',exit=0,diagnostic=False,full_flags=True,image_sha256=sha(out/'Tribes2.exe'),original_image_sha256=parent['original_image_sha256'],srw_sha256=sha(srw),patch_candidate=True,guest_loadable=False,published=False,complete_phase=True,parent_build_sha256=sha(source/'result.json'),trace_path=str(tp.relative_to(R)),trace_sha256=sha(tp),checkpoint_sha256=sha(cp),external_targets=targets,continuations=sorted(keep),relocation_adjustments=adjustments,code_calls=calls,engine_writes=engine,bank_plan_sha256=sha(bank_plan_path),bank_write_count=sum(e['image']=='bank' for e in trace['code_writes']),script_sha256=sha(pathlib.Path(__file__).resolve()),code_sha256=sha(out/'seg01_code.llinc'),overlap_entries=overlap_entries,overlap_entries_sha256=sha(out/'overlap-entries.json'),scope='Compiler-only completed string/utility patch. All recorded image/bank writes replay and every cumulative executable byte matches checkpoint/trace. Audited obsolete fixups are removed and two new absolute operands are recorded; original entry routes are retained for differential tests. No bank publication, native activation or gameplay claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
if a.prepare_native:subprocess.run([str(R/'.venv/bin/python'),str(R/'scripts/prepare-va-model.py'),str(out)],check=True)
print('Strict unpublished string/utility patch:',len(engine),'engine writes,',len(calls),'calls,',len(keep),'retained entries')
