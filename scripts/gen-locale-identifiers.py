#!/usr/bin/env python3
"""Extract pinned name/LCID indexes and neutral-resolution records."""
import argparse,hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();config=json.loads((R/'config/encoding-sources.json').read_text());refs=config['locale_reference']['sources']
for v in refs.values():assert hashlib.sha256((R/v['local_path']).read_bytes()).hexdigest()==v['sha256']
reader=config['classification_reference'];assert hashlib.sha256((R/reader['reader_local_path']).read_bytes()).hexdigest()==reader['reader_sha256']
r=(R/refs['data']['local_path']).read_bytes();u16=lambda p:struct.unpack_from('<H',r,p)[0];u32=lambda p:struct.unpack_from('<I',r,p)[0];b=u32(16);assert r[b+12:b+16]==b'NSDS';s=b+u32(b+64);records=b+u32(b+36);size=u16(b+34);count=u16(b+32)
def string(pos):
 p=s+2*pos;n=u16(p);assert p+2*(n+2)<=len(r) and u16(p+2*(n+1))==0;name=r[p+2:p+2+n*2].decode('utf-16-le');assert name.isascii() and len(name)<85;return name
names=[]
for i in range(u16(b+40)):
 pos,record,lcid=struct.unpack_from('<HHI',r,b+u32(b+48)+8*i);assert record<count;p=records+record*size;names.append({'name':string(pos),'record':record,'lcid':lcid,'default_lcid':u16(p+0x6c),'neutral':not bool(u16(p+0x18))})
normalize=lambda n:n.lower().replace('_','-');lookup={normalize(n['name']):n for n in names};assert len(lookup)==len(names)
ids=[]
for i in range(u16(b+30)):
 lcid,record,pos=struct.unpack_from('<IHH',r,b+u32(b+44)+8*i);assert record<count;p=records+record*size;canonical=string(pos) if lcid&0xf0000 else string(u32(p));default=canonical
 if not u16(p+0x18):
  specific=lookup.get(normalize(string(u32(p+0x124))))
  if specific:default=string(u32(records+specific['record']*size))
 ids.append({'lcid':lcid,'canonical':canonical,'specific':default})
assert [v['lcid'] for v in ids]==sorted(set(v['lcid'] for v in ids))
text='/* Generated pinned Wine 10.0 locale indexes; LGPL-2.1-or-later.\n See vendor/wine-nls/LICENSE and config/encoding-sources.json. */\nstatic const struct LocaleName locale_names[]={\n'+''.join(' {"%s",0x%x,0x%x,%d},\n'%(n['name'],n['lcid'],n['default_lcid'],n['neutral']) for n in names)+'};\nstatic const struct LocaleID locale_ids[]={\n'+''.join(' {0x%x,"%s","%s"},\n'%(n['lcid'],n['canonical'],n['specific']) for n in ids)+'};\n';p=R/'port/core/nls_identifiers.inc';manifest={'sources':refs,'reader_sha256':reader['reader_sha256'],'names':names,'ids':ids,'scope':'Pinned Wine locale indexes and neutral-resolution profile; shared/custom IDs are not bijective; default user/system profile en-US; no Windows execution claim'};m=R/'config/locale-identifiers-reference.json';mt=json.dumps(manifest,indent=2)+'\n'
if a.check:assert p.read_text()==text and m.read_text()==mt,'Run scripts/gen-locale-identifiers.py'
else:p.write_text(text);m.write_text(mt)
print('PASS: %d names/aliases, %d distinct indexed LCIDs'%(len(names),len(ids)))
