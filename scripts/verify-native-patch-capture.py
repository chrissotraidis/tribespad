#!/usr/bin/env python3
"""Verify native capture bytes against pinned images and the x86 patch reference.

This validates observed patches and relocated trampoline contents. It does not
install changes, infer unobserved patches or certify native execution targets.
"""
import argparse,hashlib,json,pathlib,struct
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def verify(capture,reference):
 report=json.loads(capture.read_text());plan=json.loads(reference.read_text())
 assert report['state']=='finished' and report['patch_capture_only'] and not report['native_patch_publication']
 assert report['checked_image'] and report['checked_runtime_objects'] and report['checked_support_objects'] and report['external_call_gates'] and report['independent_subpages_enabled']
 snapshots={}
 for name,row in report['patch_snapshots'].items():
  p=R/row['path'];assert sha(p)==row['sha256'] and p.stat().st_size==row['bytes'];snapshots[name]=p.read_bytes()
 assert set(snapshots)=={'engine-after.bin','qol-after.bin','patch-after.bin'}
 allocation=next(x for x in report['allocation_events'] if x['protection']==0x40)
 assert allocation['size']==65536 and allocation['type']==0x3000 and allocation['address']%65536==0
 reference_capture=R/plan['capture'];assert sha(reference_capture)==plan['capture_sha256'];trace=json.loads(reference_capture.read_text())
 images={};changes={};expected_by_image={'engine':{},'qol':{}}
 for row in plan['engine_regions']:
  before=bytes.fromhex(row['before_hex']);after=bytes.fromhex(row['after_hex']);assert len(before)==len(after)==row['size']
  for i,(a,b) in enumerate(zip(before,after)):expected_by_image['engine'][row['address']+i]=(a,b)
 for name,path in [('engine',R/'ref/control/Tribes2.exe'),('qol',R/'ref/product/IFC22.dll')]:
  assert sha(path)==plan['inputs_sha256'][name];pe=pefile.PE(str(path));original=pe.get_memory_mapped_image();after=snapshots[name+'-after.bin'];assert len(original)==len(after)
  base=pe.OPTIONAL_HEADER.ImageBase;images[name]=(base,original);changed=[]
  if name=='qol':
   working=bytearray(original)
   for event in trace['code_writes']:
    if event['image']!='qol':continue
    address=event['address'];offset=address-base;before=bytes.fromhex(event['before_bytes']);final=bytes.fromhex(event['observed_bytes'])
    assert event['instruction_completed'] and len(before)==len(final)==event['size'] and working[offset:offset+len(before)]==before
    working[offset:offset+len(final)]=final
    for i,value in enumerate(final):expected_by_image['qol'][address+i]=(original[offset+i],value)
  expected=expected_by_image[name]
  for pc,(before,final) in expected.items():assert original[pc-base]==before
  for section in pe.sections:
   if not section.Characteristics&0x20000000:continue
   for offset in range(section.VirtualAddress,section.VirtualAddress+section.SizeOfRawData):
    if original[offset]!=after[offset]:
     pc=base+offset;assert pc in expected and expected[pc]==(original[offset],after[offset]),('unreviewed code patch',name,hex(pc))
     changed.append(pc)
  changes[name]=changed
 decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
 reference_targets={t['original_target']:t for t in plan['trampolines']};data=snapshots['patch-after.bin'];assert len(data)==65536 and data[:4]==b'dtrR';trampolines=[]
 for offset in range(72,len(data)-71,72):
  block=data[offset:offset+72];moved,restored=block[30],block[54]
  if not moved and not restored:continue
  assert 0<moved<=25 and 0<restored<=22
  continuation,detour=struct.unpack_from('<II',block,64);target=continuation-restored;assert target in reference_targets
  old=reference_targets[target];assert old['continuation']==continuation and old['detour_target']==detour and old['moved_code_bytes']==moved
  base,original=images[old['original_image']];saved=original[target-base:continuation-base]
  assert len(saved)==restored and block[32:32+restored]==saved and saved.hex()==old['restore_hex']
  address=allocation['address']+offset;code=block[:moved+5];instructions=list(decoder.disasm(code,address))
  assert sum(i.size for i in instructions)==len(code) and instructions[-1].mnemonic=='jmp' and instructions[-1].operands[0].imm==continuation
  # The pinned prologues keep instruction sizes. One contains a relative CALL;
  # validate its original target after relocation instead of comparing its
  # address-dependent displacement bytes. New branch forms require review.
  assert moved==restored
  original_instructions=list(decoder.disasm(saved,target))
  reference_instructions=list(decoder.disasm(bytes.fromhex(old['code_hex'])[:moved],old['address']))
  assert len(original_instructions)==len(reference_instructions)==len(instructions)-1
  for before,ref,ins in zip(original_instructions,reference_instructions,instructions[:-1]):
   assert before.size==ref.size==ins.size
   if before.group(capstone.CS_GRP_CALL):
    assert before.size==5 and before.bytes[0]==ref.bytes[0]==ins.bytes[0]==0xe8
    assert before.operands[0].type==ref.operands[0].type==ins.operands[0].type==capstone.x86.X86_OP_IMM
    assert before.operands[0].imm==ref.operands[0].imm==ins.operands[0].imm
   else:
    assert not before.group(capstone.CS_GRP_JUMP) and bytes(before.bytes)==bytes(ref.bytes)==bytes(ins.bytes)
  trampolines.append(dict(address=address,reference_address=old['address'],original_target=target,original_image=old['original_image'],continuation=continuation,detour_target=detour,moved_code_bytes=moved,code_hex=code.hex(),restore_hex=saved.hex()))
 assert trampolines and len({t['original_target'] for t in trampolines})==len(trampolines)
 expected=expected_by_image['engine'];expected_changed={pc for pc,(a,b) in expected.items() if a!=b};observed=set(changes['engine'])
 qol_expected={pc for pc,(a,b) in expected_by_image['qol'].items() if a!=b}
 return dict(all_reference_qol_changes_observed=set(changes['qol'])==qol_expected,remaining_qol_reference_changed_bytes=len(qol_expected-set(changes['qol'])),verified=True,capture=str(capture.relative_to(R)),capture_sha256=sha(capture),reference=str(reference.relative_to(R)),reference_sha256=sha(reference),native_allocation=allocation,initialized=report['initialized'],boundary=report['result'],changed_executable_bytes={k:len(v) for k,v in changes.items()},remaining_reference_changed_bytes=len(expected_changed-observed),all_reference_engine_changes_observed=observed==expected_changed,trampolines=trampolines,missing_reference_trampoline_targets=sorted(set(reference_targets)-{t['original_target'] for t in trampolines}),native_patch_publication=False,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Every observed native engine-code byte matches the reviewed final reference; every observed trampoline preserves original prologue bytes and jumps to its exact original continuation at its native-selected guest address. Missing work remains explicit. No sequence/intermediate-write equivalence, future-patch coverage, Windows desktop parity or ARM64 trampoline execution claim.')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=pathlib.Path);p.add_argument('--reference',type=pathlib.Path,default=R/'generated/qol-patch-capture/plan.json');p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();result=verify(a.capture.resolve(),a.reference.resolve());a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ['verified','initialized','changed_executable_bytes','remaining_reference_changed_bytes','all_reference_engine_changes_observed','missing_reference_trampoline_targets']},indent=2));print('Verified relocated trampolines:',len(result['trampolines']))
