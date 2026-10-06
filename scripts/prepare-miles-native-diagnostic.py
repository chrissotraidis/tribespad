#!/usr/bin/env python3
"""Keep the pinned Miles DLL as original-address native AOT with explicit gap stops."""
import argparse,hashlib,importlib.util,json,pathlib,re,subprocess
import pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('lift',type=pathlib.Path);ap.add_argument('--codec');a=ap.parse_args();work=a.lift.resolve()
identity=json.loads((work/'result.json').read_text());assert identity['exit']==0 and identity['full_flags'] and identity['diagnostic']
codec=next((r for r in json.loads((R/'config/runtime/miles-codecs.json').read_text()) if r['module']==a.codec),None) if a.codec else None
assert not a.codec or codec
image=work/(pathlib.Path(codec['path']).name if codec else 'Mss32.dll');assert sha(image)==identity['image_sha256']==sha(R/(codec['product_path'] if codec else 'ref/product/Mss32.dll'))
if codec:assert sha(image)==codec['sha256']
pe=pefile.PE(str(image));base=pe.OPTIONAL_HEADER.ImageBase;assert not hasattr(pe,'DIRECTORY_ENTRY_TLS')
if not codec:assert base==0x21100000
out=work/'native-diagnostic';out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
main=(work/((a.codec or 'mss32')+'.llasm')).read_text().splitlines();extern=(work/'extern.llinc').read_text();known=set(re.findall(r'^proc (\S+) external','\n'.join(main)+'\n'+extern,re.M))
imports=[]
for dll in pe.DIRECTORY_ENTRY_IMPORT:
 for imp in dll.imports:
  assert imp.name and imp.name.decode() in known
  name=imp.name.decode();imports.append(dict(slot=imp.address,name=name,sr_name=name,dll=dll.dll.decode().lower(),symbol=va.symbol(name),ordinal=None))
imports.sort(key=lambda r:r['slot']);assert len(imports)*16<0x10000
for i,row in enumerate(imports):row['guest_va']=(codec['import_base'] if codec else 0xfff10000)+16*i
values={r['name']:r['guest_va'] for r in imports};assert len(values)==len(imports)
exports={e.name.decode():base+e.address for e in (pe.DIRECTORY_ENTRY_EXPORT.symbols if hasattr(pe,'DIRECTORY_ENTRY_EXPORT') else []) if e.name and not e.forwarder}
names={name:'loc_%X'%address for name,address in exports.items()}
for name,address in exports.items():
 spelling=re.sub(r'^_(.*)@\d+$',r'\1',name)
 assert spelling not in names or names[spelling]=='loc_%X'%address
 names[spelling]='loc_%X'%address
extern='\n'.join(line for line in extern.splitlines() if not(re.fullmatch(r'proc (\S+) external',line) and line.split()[1] in values))
(out/'extern.llinc').write_text('\n'.join('define '+r['name']+' '+r['symbol']+'\nproc '+r['symbol']+' external' for r in imports)+'\n'+extern+'\nproc tribes_dispatch external\nfuncv tribes_set_dispatch_target $, target\n')
with (out/'extern.llinc').open('a') as f:f.write('funcv tribes_miles_push_flat_segment $\nfuncv tribes_miles_pop_flat_segment $\n')
(out/'macros.llinc').write_text('')
segments=sorted(work.glob('seg*_code.llinc'));code='\n'.join(p.read_text() for p in segments)
for name,target in names.items():code=code.replace('proc '+name+' global c_'+name+'\n','proc '+target+'\n')
code=va.replace_names(code,names)
from miles_mixer_segments import correct
segment_corrections=[]
if not codec:code,segment_corrections=correct(code,pe)
defined=set(re.findall(r'^proc (loc_[0-9A-F]+)',code,re.M));refs=set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$',code,re.M))
missing=sorted(refs-defined)
data_labels=set().union(*(set(re.findall(r'^dlabel (loc_[0-9A-F]+)$',p.read_text(),re.M)) for p in work.glob('seg*_data.llinc')))
assert set(missing)<=data_labels,missing
for name in missing:code+='\nproc '+name+'\ncall halopad_trap_unimplemented '+hex(int(name[4:],16))+'\nendp\n'
traps=re.findall(r'call halopad_trap_unimplemented (0x[0-9a-f]+)',code)
code,stats=va.transform(code.splitlines(),values);(out/'code.llinc').write_text('\n'.join(code)+'\n')
kept=[];skip=False;included=False
for line in main:
 if re.fullmatch(r'proc (\S+) external',line) and line.split()[1] in values:continue
 if line.startswith('datasegment '):skip=True;continue
 if skip:
  if line.startswith('endd'):skip=False
  continue
 if line.startswith('define '):continue
 if re.fullmatch(r'include seg\d+_code.llinc',line):
  if not included:kept.append('include code.llinc');included=True
  continue
 kept.append(line)
assert included
(out/'module.llasm').write_text('\n'.join(kept)+'\n')
tool=R/'generated/tools/llasm/llasm';assert sha(tool)==json.loads((R/'toolchains.lock.json').read_text())['artifacts']['llasm/llasm']
with (out/'llasm.log').open('w') as log:subprocess.run([str(tool),'-m64','-ptrofs','-O2','-I',str(out),'-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'module.llasm')],cwd=out,stdout=log,stderr=log,check=True)
procs,removed=va.postprocess(out/'raw.ll',out/'module.ll');entries=sorted((int(n[4:],16),n) for n in procs);assert len(entries)==len(set(v for v,n in entries))
code_exports={n:v for n,v in exports.items() if pe.get_section_by_rva(v-base).Characteristics&0x20000000};assert set(code_exports.values())<={v for v,n in entries}
entry=base+pe.OPTIONAL_HEADER.AddressOfEntryPoint;assert entry in {v for v,n in entries}
(out/'image.bin').write_bytes(pe.get_memory_mapped_image()+bytes(pe.OPTIONAL_HEADER.SizeOfImage-len(pe.get_memory_mapped_image())))
report=dict(segment_corrections=segment_corrections,prepared=True,diagnostic_only=True,playable=False,image_sha256=sha(image),image_size=pe.OPTIONAL_HEADER.SizeOfImage,image_base=base,entry=entry,imports=imports,exports=exports,entries=entries,unsupported_instructions=traps,stats=stats,module_sha256=sha(out/'module.ll'),mapped_sha256=sha(out/'image.bin'),source_sha256={str(p.relative_to(R)):sha(p) for p in [pathlib.Path(__file__).resolve(),R/'scripts/prepare-va-model.py',R/'scripts/miles_mixer_segments.py',R/'config/runtime/miles-mixer.json',R/'config/runtime/miles-codecs.json',work/'result.json',work/((a.codec or 'mss32')+'.llasm'),work/'extern.llinc',*segments]})
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Prepared Miles AOT:',len(entries),'entries;',len(traps),'explicit instruction stops;',out)
