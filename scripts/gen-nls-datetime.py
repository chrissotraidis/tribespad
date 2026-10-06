#!/usr/bin/env python3
"""Extract Gregorian date/time presentation data from pinned Wine locale.nls."""
import argparse,hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();refs=json.loads((R/'config/encoding-sources.json').read_text())['locale_reference']['sources']
for v in refs.values():assert hashlib.sha256((R/v['local_path']).read_bytes()).hexdigest()==v['sha256']
r=(R/refs['data']['local_path']).read_bytes();u16=lambda p:struct.unpack_from('<H',r,p)[0];u32=lambda p:struct.unpack_from('<I',r,p)[0];base=u32(16);assert r[base+12:base+16]==b'NSDS';size=u16(base+0x22);count=u16(base+0x20);data=base+u32(base+0x24);index=base+u32(base+0x30);strings=base+u32(base+0x40);pool=[];offsets={}
def string(pos):
 p=strings+2*pos;n=u16(p);assert p+2*(n+2)<=len(r);assert u16(p+2*(n+1))==0;return tuple(struct.unpack_from('<%dH'%n,r,p+2))
def intern(pos):
 val=string(pos)
 if val not in offsets:offsets[val]=len(pool);pool.extend((*val,0))
 return offsets[val]
def array(pos,n):
 p=strings+2*pos;length=u16(p);assert p+2+4*length<=len(r);return [intern(u32(p+2+4*i) if i<length else 0) for i in range(n)]
profiles=[]
for i in range(count):
 p=data+i*size;assert p+size<=len(r)
 profiles.append({'calendar':u16(strings+2*u32(p+0x7c)+2),'formats':[array(u32(p+x),1)[0] for x in [0x5c,0x60,0x64,0x140,0x58]],'days':[array(u32(p+x),7) for x in [0xbc,0xc0]],'months':[array(u32(p+x) or (u32(p+0xc4) if x==0xcc else u32(p+0xc8) if x==0xd0 else 0),12) for x in [0xc4,0xc8,0xcc,0xd0]],'meridian':[intern(u32(p+x)) for x in [0x4c,0x50]],'era':[intern(u32(p+x)) for x in [0x108,0x10c]]})
names=[]
for i in range(u16(base+0x28)):
 pos,idx,lcid=struct.unpack_from('<HHI',r,index+8*i);name=''.join(map(chr,string(pos)));assert name.isascii() and idx<count;names.append((name,idx))
text='/* Generated from pinned Wine 10.0 locale.nls. LGPL-2.1-or-later.\n   '+refs['data']['sha256']+'; see vendor/wine-nls/LICENSE. */\nstatic const uint16_t dt_strings[]={\n'+''.join(' '+','.join('0x%04x'%x for x in pool[i:i+16])+',\n' for i in range(0,len(pool),16))+'};\nstatic const struct DateLocale dt_locales[]={\n'
for p in profiles:
 arr=lambda x:'{'+','.join(map(str,x))+'}'
 text+=' {%d,%s,%s,%s,%s,%s},\n'%(p['calendar'],arr(p['formats']),'{'+','.join(arr(v) for v in p['days'])+'}','{'+','.join(arr(v) for v in p['months'])+'}',arr(p['meridian']),arr(p['era']))
text+='};\nstatic const struct DateName dt_names[]={\n'+''.join(' {"%s",%d},\n'%v for v in names)+'};\n';p=R/'port/core/nls_datetime.inc'
if a.check:assert p.read_text()==text,'Run scripts/gen-nls-datetime.py'
else:p.write_text(text)
print('PASS: %d locale profiles, %d names/aliases, %d UTF-16 units of presentation data'%(len(profiles),len(names),len(pool)))
