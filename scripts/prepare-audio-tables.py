#!/usr/bin/env python3
"""Strictly compile the original finite OpenAL table/source-limit operand batch."""
import bisect,hashlib,json,os,pathlib,re,shutil,struct,subprocess,sys
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/audio-tables';P.mkdir(exist_ok=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=R/'generated/integration-review/string-patched-engine';image=source/'Tribes2.exe';parent=json.loads((source/'result.json').read_text());assert sha(image)==parent['image_sha256']
qpath=R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll';q=pefile.PE(str(qpath));p=pefile.PE(str(image));original=pefile.PE(str(R/'ref/control/Tribes2.exe'))
audit=R/'generated/integration-review/openal-provider/audio-table-writes-static.json';m=json.loads(audit.read_text());assert sha(qpath)==m['qol_sha256'] and sha(R/'ref/control/Tribes2.exe')==m['engine_sha256']
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.detail=True
writes=[]
for group in m['groups']:
 pc=group['writer'];ins=next(md.disasm(q.get_data(pc-0x10000000,16),pc));assert ins.mnemonic=='mov' and ins.operands[0].mem.base==capstone.x86.X86_REG_EDI and ins.operands[0].size==group['size'] and ins.operands[1].imm==group['value']
 rows=[w for w in m['writes'] if w['table']==group['table']];assert len(rows)==group['count']
 for i,w in enumerate(rows):
  assert w['index']==i and int.from_bytes(q.get_data(group['table']-0x10000000+4*i,4),'little')==w['address']
  assert w['executable'] and w['size']==group['size'] and w['value']==group['value']
  before=bytes.fromhex(w['before']);assert p.get_data(w['address']-0x400000,w['size'])==before
  writes.append(dict(pc=pc,address=w['address'],size=w['size'],before=int.from_bytes(before,'little'),value=w['value']))
 assert q.get_data(group['table']-0x10000000+4*len(rows),4)==bytes(4)
assert len(writes)==160 and len({w['address'] for w in writes})==160
raw=bytearray(image.read_bytes())
for w in writes:
 at=p.get_offset_from_rva(w['address']-0x400000);raw[at:at+w['size']]=w['value'].to_bytes(w['size'],'little')
# These new operands address fixed guest DLL data, not engine-relative symbols.
# Disable only their compiler-input relocations; runtime bytes stay exact.
removed=set();relocations=[]
for line in (source/'relocations.csv').read_text().splitlines():
 at,target=[int(x,0) for x in line.split(',')[:2]]
 hits=[w for w in writes if w['address']<at+4 and at<w['address']+w['size']]
 if hits:
  assert len(hits)==1 and hits[0]['address']==at and hits[0]['size']==4 and hits[0]['before']==target
  removed.add(at)
 else:relocations.append(line)
assert len(removed)==128 and {w['address'] for w in writes if w['size']==4}-removed=={0x407761,0x40522b}
removed.update([0x407761,0x40522b])
disabled=set()
for block in p.DIRECTORY_ENTRY_BASERELOC:
 for e in block.entries:
  if 0x400000+e.rva in removed:
   assert e.type==3;struct.pack_into('<H',raw,e.struct.get_file_offset(),e.rva&4095);disabled.add(0x400000+e.rva)
assert disabled==removed and len(removed)==130
(P/'Tribes2.exe').write_bytes(raw);(P/'relocations.csv').write_text('\n'.join(relocations)+'\n')
for path in source.glob('*.sci'):shutil.copy2(path,P/path.name)
shutil.copy2(source/'literal_fixups.csv',P/'literal_fixups.csv')
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
if '--reuse-lift' not in sys.argv:
 with (P/'srw.log').open('w') as log:
  run=subprocess.run([str(srw),'Tribes2.exe','tribes2.llasm'],cwd=P,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'},stdout=log,stderr=log,timeout=600)
 assert not run.returncode,P/'srw.log'
code=(P/'seg01_code.llinc').read_text();assert 'halopad_trap_unimplemented' not in code
main=(P/'tribes2.llasm').read_text();includes=''.join('include %s\n'%n for n in ['tribes_mmx.llinc','tribes_sse.llinc','tribes_scan.llinc','tribes_x87.llinc','tribes_shift.llinc','tribes_atomic.llinc']);(P/'tribes2.llasm').write_text(includes+main)
shutil.copy2(source/'extern.llinc',P/'extern.llinc');(P/'macros.llinc').write_text('')
(P/'result.json').write_text(json.dumps(dict(exit=0,full_flags=True,diagnostic=False,published=False,guest_loadable=False,image_sha256=sha(P/'Tribes2.exe')))+'\n')
if '--reuse-lift' not in sys.argv:subprocess.run([str(R/'.venv/bin/python'),str(R/'scripts/prepare-va-model.py'),str(P)],check=True)
vm=json.loads((P/'va/result.json').read_text());assert vm['image_sha256']==sha(P/'Tribes2.exe') and vm['engine_ll_sha256']==sha(P/'va/engine.ll')
def functions(path):
 with path.open() as f:
  name=None;lines=[]
  for line in f:
   if line.startswith('define '):assert name is None;name=re.search(r'@(\w+)\(',line)[1];lines=[]
   if name:
    lines.append(line)
    if line.strip()=='}':yield name,''.join(lines);name=None
 assert name is None
def semantic(body):return re.sub(r'^define protected ','define hidden ',body)
old={n:hashlib.sha256(semantic(b).encode()).hexdigest() for n,b in functions(source/'va/engine.ll')};new={};selected={}
for n,b in functions(P/'va/engine.ll'):
 new[n]=hashlib.sha256(semantic(b).encode()).hexdigest()
 if old.get(n)!=new[n]:selected[n]=b
assert set(new)==set(old),('Entry labels changed',set(new)^set(old))
# Every selected block contains an actual changed instruction operand, and every
# original operand decodes at its original width. No manually rewritten flags.
entries=sorted(int(n[4:],16) for n in old if n.startswith('loc_'));covered=set();records=[]
for n,b in selected.items():
 entry=int(n[4:],16);end=entries[bisect.bisect_right(entries,entry)];ws=[w for w in writes if entry<=w['address']<end];assert ws,n
 before=p.get_data(entry-0x400000,end-entry);after=bytearray(before)
 instructions=list(md.disasm(before,entry))
 for w in ws:
  matches=[i for i in instructions if (i.disp_size and i.address+i.disp_offset<=w['address'] and w['address']+w['size']<=i.address+i.disp_offset+i.disp_size) or (i.imm_size and i.address+i.imm_offset<=w['address'] and w['address']+w['size']<=i.address+i.imm_offset+i.imm_size)];assert len(matches)==1,(n,w)
  at=w['address']-entry;after[at:at+w['size']]=w['value'].to_bytes(w['size'],'little');covered.add(w['address'])
 records.append(dict(name=n,entry=entry,end=end,before_sha256=old[n],after_sha256=new[n],before_bytes=before.hex(),after_bytes=after.hex(),writes=[w['address'] for w in ws]))
assert covered=={w['address'] for w in writes}
with (P/'va/engine.ll').open() as f:
 prefix=''
 for line in f:
  if line.startswith('define '):break
  prefix+=line
body=''.join(selected.values());refs=set(re.findall(r'@(\w+)\(',body))-set(selected);declared=set(re.findall(r'@(\w+)\(',prefix))
assert all(re.fullmatch('loc_[0-9A-F]+',n) for n in refs-declared)
(P/'engine.ll').write_text(prefix+''.join('declare hidden fastcc void @%s(%%_cpu*)\n'%n for n in sorted(refs-declared))+body)
inputs=[image,source/'va/engine.ll',qpath,audit,pathlib.Path(__file__).resolve(),srw,R/'scripts/prepare-va-model.py']
report=dict(passed=True,published=False,records=records,writes=writes,inputs_sha256={str(f.relative_to(R)):sha(f) for f in inputs},outputs_sha256={'engine.ll':sha(P/'engine.ll')},scope='Strict translated original audio operands; no runtime admission or gameplay claim.')
(P/'preparation.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared',len(records),'strict audio-table definitions /160 writes')
