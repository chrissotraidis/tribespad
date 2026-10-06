#!/usr/bin/env python3
"""Strictly lift the captured later engine variant; never load it as guest data."""
import argparse,hashlib,json,os,pathlib,re,shutil,struct,subprocess
import pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--prepare-native',action='store_true');a=p.parse_args()
source=R/'generated/integration-review/patched-engine';parent=json.loads((source/'result.json').read_text());assert parent['state']=='strictly_lifted_unpublished' and not parent['guest_loadable']
assert sha(source/'Tribes2.exe')==parent['image_sha256'] and sha(R/'scripts/prepare-patched-engine.py')==parent['script_sha256']
plan_path=R/'generated/integration-review/late-patch-plan.json';plan=json.loads(plan_path.read_text());assert plan['verified'] and not plan['native_activated']
assert sha(R/plan['trace_path'])==plan['trace_sha256'] and sha(R/plan['checkpoint_path'])==plan['checkpoint_sha256']
assert sha(R/'scripts/build-late-patch-plan.py')==plan['script_sha256'] and plan['initial_plan_sha256']==parent['native_plan_sha256']
out=R/'generated/integration-review/late-patched-engine';out.mkdir(exist_ok=True);report_path=out/'result.json';report_path.write_text(json.dumps(dict(state='preparing',published=False))+'\n')
raw=bytearray((source/'Tribes2.exe').read_bytes());pe=pefile.PE(data=bytes(raw));base=pe.OPTIONAL_HEADER.ImageBase
for row in plan['engine_regions']:
 offset=pe.get_offset_from_rva(row['address']-base);before=bytes.fromhex(row['before_hex']);after=bytes.fromhex(row['after_hex']);assert len(before)==len(after)==row['size']
 assert raw[offset:offset+len(before)]==before,('Compiler input disagrees with live preimage',hex(row['address']))
 raw[offset:offset+len(after)]=after
relocations=[];adjustments=[];removed=set()
for line in (source/'relocations.csv').read_text().splitlines():
 address,target=[int(v,0) for v in line.split(',')[:2]]
 if any(address<r['address']+r['size'] and address+4>r['address'] for r in plan['engine_regions']):
  offset=pe.get_offset_from_rva(address-base);after=bytes(raw[offset:offset+4])
  if after==b'\x90'*4:removed.add(address);adjustments.append(dict(address=address,old_target=target,action='removed_from_NOP_fill'));continue
  assert address==0x799b24 and int.from_bytes(after,'little')==0x100600c0
  adjustments.append(dict(address=address,old_target=target,new_target=0x100600c0,action='updated_DLL_callback'));target=0x100600c0
 relocations.append('0x%x,0x%x,\n'%(address,target))
disabled=set()
for block in pe.DIRECTORY_ENTRY_BASERELOC:
 for entry in block.entries:
  if base+entry.rva in removed:
   assert entry.type==3;struct.pack_into('<H',raw,entry.struct.get_file_offset(),entry.rva&0xfff);disabled.add(base+entry.rva)
assert disabled==removed
(out/'Tribes2.exe').write_bytes(raw);(out/'relocations.csv').write_text(''.join(relocations))
for path in source.glob('*.sci'):shutil.copy2(path,out/path.name)
if (source/'literal_fixups.csv').exists():shutil.copy2(source/'literal_fixups.csv',out/'literal_fixups.csv')
targets=sorted(set(parent['external_targets'])|{r['new_target'] for r in plan['code_calls']}|set(plan['data_targets']))
(out/'external_procedures.sci').write_text(''.join('loc_%X,loc_%X\n'%(v,v) for v in targets))
# Preserve old continuation labels around replaced instructions. They may still
# be referenced by the pre-patch engine variant until the publication boundary.
old_entries=sorted(int(x,16) for x in re.findall(r'^proc loc_([0-9A-F]+)\b',(source/'seg01_code.llinc').read_text(),re.M));keep=set(parent['continuations'])
for row in plan['engine_regions']:
 if row['category']!='code':continue
 begin,end=row['address'],row['address']+row['size'];keep.update(x for x in old_entries if begin<=x<end)
 keep.add(max(x for x in old_entries if x<=begin));keep.add(next(x for x in old_entries if x>=end))
(out/'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n'%(v,v) for v in sorted(keep)))
srw=R/'generated/tools/SRW-tribes/SRW.exe';assert sha(srw)==parent['srw_sha256'];env={k:v for k,v in os.environ.items() if k!='HALOPAD_SRW_DIAG'};env['TRIBES_SRW_FULL_FLAGS']='1'
with (out/'srw.log').open('w') as log:run=subprocess.run([str(srw),'Tribes2.exe','tribes2.llasm'],cwd=out,env=env,stdout=log,stderr=log,timeout=600)
assert run.returncode==0,out/'srw.log'
code=(out/'seg01_code.llinc').read_text();assert 'halopad_trap_unimplemented' not in code
for row in plan['code_calls']:assert 'PUSH loc_%X\ntcall loc_%X\n'%(row['call_site']+5,row['new_target']) in code,row
assert all(re.search(r'^proc loc_%X\b'%v,code,re.M) for v in keep)
main=(out/'tribes2.llasm').read_text();includes=''.join('include %s\n'%n for n in ['tribes_mmx.llinc','tribes_sse.llinc','tribes_scan.llinc','tribes_x87.llinc','tribes_shift.llinc','tribes_atomic.llinc']);(out/'tribes2.llasm').write_text(includes+main)
known=set(re.findall(r'^proc (\S+) external',main,re.M));imports={i.name.decode() for d in pe.DIRECTORY_ENTRY_IMPORT for i in d.imports if i.name}
(out/'extern.llinc').write_text(''.join('proc %s external\n'%n for n in sorted(imports-known))+''.join('proc loc_%X external\n'%v for v in targets));(out/'macros.llinc').write_text('')
report=dict(state='strictly_lifted_unpublished',exit=0,diagnostic=False,full_flags=True,image_sha256=sha(out/'Tribes2.exe'),original_image_sha256=parent['original_image_sha256'],srw_sha256=sha(srw),patch_candidate=True,guest_loadable=False,published=False,parent_build_sha256=sha(source/'result.json'),late_plan_sha256=sha(plan_path),external_targets=targets,continuations=sorted(keep),relocation_adjustments=adjustments,script_sha256=sha(pathlib.Path(__file__).resolve()),code_sha256=sha(out/'seg01_code.llinc'),scope='Compiler-only later variant covering observed code changes and callback data pointer. No native staged activation or game target execution claim.')
report_path.write_text(json.dumps(report,indent=2)+'\n')
if a.prepare_native:subprocess.run([str(R/'.venv/bin/python'),str(R/'scripts/prepare-va-model.py'),str(out)],check=True)
print(json.dumps(report,indent=2))
