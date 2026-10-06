#!/usr/bin/env python3
"""Extract actual Wine 10.0 installed locale names, flags and order."""
import argparse,hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();refs=json.loads((R/'config/encoding-sources.json').read_text())['locale_reference']['sources']
for v in refs.values():assert hashlib.sha256((R/v['local_path']).read_bytes()).hexdigest()==v['sha256']
r=(R/refs['data']['local_path']).read_bytes();u16=lambda p:struct.unpack_from('<H',r,p)[0];u32=lambda p:struct.unpack_from('<I',r,p)[0];base=u32(16);assert r[base+12:base+16]==b'NSDS';size=u16(base+0x22);records=u16(base+0x20);data=base+u32(base+0x24);names=u16(base+0x28);index=base+u32(base+0x30);strings=base+u32(base+0x40);assert data+records*size<=len(r) and index+names*8<=len(r)
rows=[];aliases=[]
for i in range(names):
 name,idx,lcid=struct.unpack_from('<HHI',r,index+8*i);assert idx<records;ptr=strings+2*name;count=u16(ptr);assert count<85 and ptr+2*(count+2)<=len(r);text=r[ptr+2:ptr+2*(count+1)].decode('utf-16-le');assert u16(ptr+2*(count+1))==0 and text.isascii()
 if lcid&0x80000000:aliases.append(text);continue
 neutral=u16(data+idx*size+0x18);assert neutral in [0,1];flags=4 if ((lcid>>16)&15 or '_' in text) else 1|(32 if neutral else 16);rows.append({'name':text,'flags':flags,'lcid':lcid,'data_index':idx})
assert len({v['name'].lower() for v in rows})==len(rows)
manifest={'source_sha256':refs['data']['sha256'],'header_sha256':refs['header']['sha256'],'entries':rows,'alias_names_skipped':aliases,'scope':'Pinned Wine 10.0 name-index order, alias exclusion, alternate-sort and inverted-neutral metadata; no Apple/Windows installed population claim'}
text='/* Generated Wine 10.0 locale.nls names and flags. LGPL-2.1-or-later.\n   Source SHA256 '+refs['data']['sha256']+'. See vendor/wine-nls/LICENSE. */\nstatic const struct LocaleRow locale_rows[]={\n'+''.join(' {"%s",0x%x},\n'%(v['name'],v['flags']) for v in rows)+'};\n'
for p,value in [(R/'port/core/nls_locales.inc',text),(R/'config/locale-enumeration-reference.json',json.dumps(manifest,indent=2)+'\n')]:
 if a.check:assert p.read_text()==value,'Run scripts/gen-nls-locales.py'
 else:p.write_text(value)
print('PASS: %d locales, %d aliases excluded, flags %s'%(len(rows),len(aliases),{str(x):sum(v['flags']==x for v in rows) for x in (4,17,33)}))
