#!/usr/bin/env python3
"""Verify observed code-write preimages and prepare a partial AOT input candidate.
Never install this prefix candidate as the selected product engine. Completing process attach under the diagnostic loader is not proof of future
runtime patches, native initialization, rendering or a game session.
"""
import argparse,hashlib,json,pathlib,struct
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--capture',default='generated/qol-initialization/executable-capture-no-sse2.json');a=ap.parse_args();source=R/a.capture;r=json.loads(source.read_text());assert r['state']=='finished' and r['executable_reference']['enabled']
out=R/'generated/qol-patch-capture';out.mkdir(exist_ok=True);plan_path=out/'plan.json';plan_path.write_text(json.dumps({'state':'building','capture':a.capture})+'\n');images={}
for name,path in [('engine',R/'ref/control/Tribes2.exe'),('qol',R/'ref/product/IFC22.dll')]:
 raw=path.read_bytes();assert hashlib.sha256(raw).hexdigest()==r['inputs_sha256'][name];pe=pefile.PE(data=raw);images[name]=(pe,raw,bytearray(pe.get_memory_mapped_image()))
decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
original_images={name:bytes(image) for name,(pe,raw,image) in images.items()}
engine_writes=[];touched=set();calls=[];raw_engine=bytearray(images['engine'][1]);working=images['engine'][2];engine_base=images['engine'][0].OPTIONAL_HEADER.ImageBase
for e in r['code_writes']:
 if e['image']!='engine':continue
 assert e['instruction_completed'];offset=e['address']-engine_base;before=bytes.fromhex(e['before_bytes']);after=bytes.fromhex(e['observed_bytes']);assert len(before)==len(after)==e['size'];assert working[offset:offset+e['size']]==before,('preimage mismatch',e)
 assert e['size']>0 and any(section.Characteristics&0x20000000 and section.VirtualAddress<=offset and offset+e['size']<=section.VirtualAddress+section.SizeOfRawData for section in images['engine'][0].sections),'write outside file-backed executable section'
 classification='byte_patch_requires_instruction_rebuild'
 if e['size']==4 and working[offset-1]==0xe8:
  original=bytes(working[offset-1:offset+4]);patched=b'\xe8'+after;old=engine_base+offset+4+struct.unpack('<i',before)[0];new=engine_base+offset+4+struct.unpack('<i',after)[0]
  instruction=list(decoder.disasm(patched,e['address']-1));assert len(instruction)==1 and instruction[0].mnemonic=='call' and instruction[0].operands[0].imm==new
  call={'call_site':e['address']-1,'old_target':old,'new_target':new,'writer_pc':e['pc'],'before_hex':original.hex(),'after_hex':patched.hex(),'required_aot_change':'Recompile/route this original call site to verified QoL implementation; writing x86 bytes alone changes no existing ARM64 call'};calls.append(call);classification='relative_call_operand'
 engine_writes.append({'address':e['address'],'size':e['size'],'writer_pc':e['pc'],'before_hex':before.hex(),'after_hex':after.hex(),'classification':classification})
 touched.update(range(offset,offset+e['size']))
 working[offset:offset+e['size']]=after;file_offset=images['engine'][0].get_offset_from_rva(offset);assert raw_engine[file_offset:file_offset+e['size']]==before;raw_engine[file_offset:file_offset+e['size']]=after
assert calls
candidate=out/'engine-code-prefix-candidate.exe';candidate.write_bytes(raw_engine);candidate_pe=pefile.PE(data=raw_engine);assert candidate_pe.OPTIONAL_HEADER.ImageBase==engine_base and candidate_pe.OPTIONAL_HEADER.SizeOfImage==images['engine'][0].OPTIONAL_HEADER.SizeOfImage
trampolines=[]
for snapshot in r['executable_reference']['snapshots']:
 data=(R/snapshot['path']).read_bytes();assert len(data)==snapshot['bytes'] and hashlib.sha256(data).hexdigest()==snapshot['sha256'];assert data[:4]==b'dtrR'
 for offset in range(72,len(data)-71,72):
  block=data[offset:offset+72];code_size,restore_size=block[30],block[54]
  if not code_size or not restore_size:continue
  assert code_size<=30 and restore_size<=22
  remain,detour=struct.unpack_from('<2I',block,64);target=remain-restore_size
  original=None;owner=None
  for name,(pe,raw,image) in images.items():
   base=pe.OPTIONAL_HEADER.ImageBase
   if base<=target and remain<=base+len(image):original=original_images[name][target-base:remain-base];owner=name;break
  assert original is not None and block[32:32+restore_size]==original,('trampoline original-byte mismatch',hex(target))
  address=snapshot['address']+offset
  assert code_size+5<=30 and block[code_size]==0xe9,('missing trampoline continuation',hex(address))
  code=block[:code_size+5];instructions=list(decoder.disasm(code,address));assert sum(i.size for i in instructions)==len(code)
  assert instructions[-1].mnemonic=='jmp' and instructions[-1].operands[0].imm==remain,('continuation mismatch',hex(address))
  trampolines.append({'address':address,'original_target':target,'original_image':owner,'continuation':remain,'detour_target':detour,'restore_hex':original.hex(),'moved_code_bytes':code_size,'code_hex':code.hex(),'instructions':[{'pc':i.address,'bytes_hex':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str} for i in instructions],'executed_in_capture':any(address<=e['pc']<address+code_size for e in r['executable_reference']['executed_instructions']),'scope':'Layout corroborated with upstream Detours x86 72-byte structure; embedded exact version unproven'})
# Every raw write is preserved in order; final contiguous byte regions include
# NOP fills and Detours entry redirects, not just relative CALL operands.
spans=[]
for offset in sorted(touched):
 if spans and offset==spans[-1][1]:spans[-1][1]+=1
 else:spans.append([offset,offset+1])
regions=[{'address':engine_base+start,'size':end-start,'before_hex':original_images['engine'][start:end].hex(),'after_hex':bytes(working[start:end]).hex(),'required_aot_change':'Rebuild instruction/control-flow analysis with complete surrounding function context; this byte range alone is not an instruction boundary or native implementation'} for start,end in spans]
entry_redirects=[]
for trampoline in trampolines:
 if trampoline['original_image']!='engine':continue
 offset=trampoline['original_target']-engine_base;patch=bytes(working[offset:offset+5])
 if patch[0]!=0xe9:continue
 instructions=list(decoder.disasm(patch,trampoline['original_target']));assert len(instructions)==1 and instructions[0].mnemonic=='jmp'
 assert instructions[0].operands[0].imm==trampoline['detour_target'],'final entry redirect disagrees with captured trampoline'
 entry_redirects.append({'entry':trampoline['original_target'],'detour_target':trampoline['detour_target'],'trampoline':trampoline['address'],'before_hex':original_images['engine'][offset:offset+5].hex(),'after_hex':patch.hex(),'required_aot_change':'Publish verified precompiled detour entry and separately precompile the captured relocated original trampoline; guest instruction-cache flushing cannot substitute for ARM64 routing'})
plan={'plan_builder_sha256':hashlib.sha256(pathlib.Path(__file__).resolve().read_bytes()).hexdigest(),'state':'verified process-attach capture' if r['initialized'] else 'verified partial capture','diagnostic_initialization_completed':r['initialized'],'capture_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'capture':a.capture,'inputs_sha256':r['inputs_sha256'],'engine_writes':engine_writes,'engine_regions':regions,'entry_redirects':entry_redirects,'code_calls':calls,'trampolines':trampolines,'image_data_write_counts':{name:sum(e['image']==name for e in r['image_data_writes']) for name in images},'candidate':{'path':str(candidate.relative_to(R)),'sha256':hashlib.sha256(candidate.read_bytes()).hexdigest(),'installed':False,'native_arm64_compiled':False},'boundary':r['stages'][-1]['stop'],'scope':'Only observed engine executable-section prefix writes, with sequential preimage guards; no native initializer, future runtime-patch coverage, native QoL publication, trampoline execution equivalence or game-session claim'}
plan_path.write_text(json.dumps(plan,indent=2)+'\n');print(json.dumps({'calls':len(calls),'trampolines':len(trampolines),'boundary':plan['boundary'].get('name',plan['boundary']['kind']),'candidate':plan['candidate'],'scope':plan['scope']},indent=2))
