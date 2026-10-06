#!/usr/bin/env python3
"""Extract immutable locale information from pinned Wine 10.0 NLS data."""
import argparse,hashlib,json,pathlib,re,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();config=json.loads((R/'config/encoding-sources.json').read_text());refs=config['locale_reference']['sources']
for v in refs.values():assert hashlib.sha256((R/v['local_path']).read_bytes()).hexdigest()==v['sha256']
reader=config['classification_reference'];assert hashlib.sha256((R/reader['reader_local_path']).read_bytes()).hexdigest()==reader['reader_sha256']
header=R/'ref/downloads/wine-10.0-winnls.h';assert hashlib.sha256(header.read_bytes()).hexdigest()==next(v['sha256'] for v in config['collation_reference']['supporting_sources'].values() if v['local_path']=='ref/downloads/wine-10.0-winnls.h')
constants={k:int(v.rstrip('lL'),16) for k,v in re.findall(r'^#define\s+(LOCALE_\w+)\s+(0[xX][0-9a-fA-F]+)',header.read_text(),re.M)}
constants.update({k:int(v) for k,v in re.findall(r'^#define\s+(LOCALE_\w+)\s+([0-9]+)\s',header.read_text(),re.M)})
for k,v in re.findall(r'^#define\s+(LOCALE_\w+)\s+(LOCALE_\w+)',header.read_text(),re.M):
 if v in constants:constants[k]=constants[v]
r=(R/refs['data']['local_path']).read_bytes();u16=lambda p:struct.unpack_from('<H',r,p)[0];u32=lambda p:struct.unpack_from('<I',r,p)[0];base=u32(16);assert r[base+12:base+16]==b'NSDS';size=u16(base+0x22);count=u16(base+0x20);data=base+u32(base+0x24);index=base+u32(base+0x30);strings=base+u32(base+0x40)
def units(pos):
 p=strings+2*pos;n=u16(p);assert p+2*(n+1)<=len(r);return tuple(struct.unpack_from('<%dH'%n,r,p+2))
def array(pos,i):
 p=strings+2*pos;n=u16(p);assert p+2+4*n<=len(r);return units(u32(p+2+4*i)) if i<n else ()
# Every mapping is a documented field in the pinned internal header. Arrays,
# binary values and derived values are handled explicitly below, not guessed.
layout=(R/refs['header']['local_path']).read_text();block=layout[layout.index('UINT   sname;'):layout.index('} NLS_LOCALE_DATA;')];fields={};spec={}
for primitive,field,offset,constant in re.findall(r'(UINT|USHORT)\s+(\w+);\s*/\*\s*([0-9a-f]+)\s+(LOCALE_\w+)',block):
 off=int(offset,16);fields[field]=(off,primitive)
 if field in ('sgrouping','smongrouping','snativedigits','fontsignature','sdayname','sabbrevdayname','smonthname','sabbrevmonthname','sshortestdayname','scalendartype'):continue
 spec[constants[constant]]=(0 if primitive=='USHORT' or field=='igeoid' else 1,field,0)
# Name aliases and fields sharing the same stored value.
for constant,field in [('LOCALE_IDEFAULTCOUNTRY','icountry'),('LOCALE_IINTLCURRDIGITS','icurrdigits'),('LOCALE_SLOCALIZEDDISPLAYNAME','sengdisplayname'),('LOCALE_SLOCALIZEDLANGUAGENAME','senglanguage'),('LOCALE_SLOCALIZEDCOUNTRYNAME','sengcountry')]:spec[constants[constant]]=(0 if field.startswith('i') else 1,field,0)
for field,constant in [('sdayname','LOCALE_SDAYNAME1'),('sabbrevdayname','LOCALE_SABBREVDAYNAME1'),('sshortestdayname','LOCALE_SSHORTESTDAYNAME1')]:
 for i in range(7):spec[constants[constant]+i]=(1,field,(i+1)%7)
for field,constant,last,gen in [('smonthname','LOCALE_SMONTHNAME1','LOCALE_SMONTHNAME13',0xcc),('sabbrevmonthname','LOCALE_SABBREVMONTHNAME1','LOCALE_SABBREVMONTHNAME13',0xd0)]:
 for i in range(13):
  key=constants[constant]+i if i<12 else constants[last];spec[key]=(1,field,i);spec[key|0x10000000]=(1,'gen'+field,i)
for constant,field in [('LOCALE_SGROUPING','sgrouping'),('LOCALE_SMONGROUPING','smongrouping'),('LOCALE_SNATIVEDIGITS','snativedigits')]:spec[constants[constant]]=(1,field,0)
spec[constants['LOCALE_FONTSIGNATURE']]=(2,'fontsignature',0)
for key in ['ICALENDARTYPE','IOPTIONALCALENDAR','IDATE','ILDATE','ITIME','ITIMEMARKPOSN','ICENTURY','ITLZERO','IDAYLZERO','IMONLZERO','IPOSSIGNPOSN','INEGSIGNPOSN','IPOSSYMPRECEDES','IPOSSEPBYSPACE','INEGSYMPRECEDES','INEGSEPBYSPACE']:
 spec[constants['LOCALE_'+key]]=(0,key,0)
for key in ['SDATE','STIME','SPERMILLE']:spec[constants['LOCALE_'+key]]=(1,key,0)
# Quoted literals are skipped exactly as find_format() in the source reader.
def tokens(s,chars):
 i=0
 while i<len(s):
  if s[i]==39:
   i+=1
   while i<len(s):
    if s[i]==39:
     i+=1
     if i==len(s) or s[i]!=39:break
    i+=1
  elif chr(s[i]) in chars:yield i;i+=1
  else:i+=1
pool=[];offsets={};rows=[]
def intern(v):
 v=tuple(v)
 if v not in offsets:offsets[v]=len(pool);pool.extend((len(v),*v))
 return offsets[v]
keys=sorted(spec)
for record in range(count):
 p=data+record*size;get=lambda field:u16(p+fields[field][0]) if fields[field][1]=='USHORT' else u32(p+fields[field][0]);short=array(get('sshortdate'),0);long=array(get('slongdate'),0);time=array(get('stimeformat'),0)
 def value(field,i):
  if field in ('SDATE','STIME'):
   s,chars=(short,'dMy') if field=='SDATE' else (time,'Hhms');at=list(tokens(s,chars));assert at,(record,field);start=at[0]+1
   while start<len(s) and s[start]==s[start-1]:start+=1
   end=next((j for j in tokens(s,chars) if j>=start),len(s));return s[start:end]
  if field=='SPERMILLE':return (0x2030,)
  if field in ('IDATE','ILDATE'):
   s=short if field=='IDATE' else long;v=0
   for j in tokens(s,'dMy'):
    if s[j]==ord('M'):break
    v=2 if s[j]==ord('y') else 1
   return v
  if field in ('ITIME','ITIMEMARKPOSN','ICENTURY','ITLZERO','IDAYLZERO','IMONLZERO'):
   s,chars=(time,'Hh') if field in ('ITIME','ITLZERO') else (time,'Hhmst') if field=='ITIMEMARKPOSN' else (short,'y') if field=='ICENTURY' else (short,'d') if field=='IDAYLZERO' else (short,'M');j=next(tokens(s,chars),None);assert j is not None,(record,field)
   if field=='ITIME':return int(s[j]==ord('H'))
   if field=='ITIMEMARKPOSN':return int(s[j]==ord('t'))
   if field=='ICENTURY':return int(s[j:j+4]==(121,)*4)
   return int(j+1<len(s) and s[j+1]==s[j])
  if field in ('IPOSSIGNPOSN','INEGSIGNPOSN','INEGSYMPRECEDES'):
   tables={'IPOSSIGNPOSN':[3,3,4,2,1,1,3,4,1,3,4,2,4,3,3,1],'INEGSIGNPOSN':[0,3,4,2,0,1,3,4,1,3,4,2,4,3,0,0],'INEGSYMPRECEDES':[1,1,1,1,0,0,0,0,0,1,0,1,1,0,1,0]};return tables[field][get('inegcurr')]
  if field=='IPOSSYMPRECEDES':return int(not(get('icurrency')&1))
  if field=='IPOSSEPBYSPACE':return int(bool(get('icurrency')&2))
  if field=='INEGSEPBYSPACE':return int(get('inegcurr')>=8)
  if field in ('ICALENDARTYPE','IOPTIONALCALENDAR'):
   off=u32(p+0x7c);v=units(off);return v[0] if field=='ICALENDARTYPE' else (v[1] if len(v)>1 else 0)
  if field.startswith('gen'):
   normal=field[3:];off=u32(p+(0xcc if normal=='smonthname' else 0xd0)) or get(normal);return array(off,i)
  v=get(field)
  if field=='ilanguage':return v if get('inotneutral') else get('idefaultlanguage')
  if field=='inotneutral':return int(not v)
  if field=='ifirstdayofweek':return (v+6)%7
  if field in ('idefaultansicodepage','idefaultcodepage','idefaultmaccodepage') and v==65001:return {'idefaultansicodepage':0,'idefaultcodepage':1,'idefaultmaccodepage':2}[field]
  if fields[field][1]=='USHORT' or field=='igeoid':return v
  if field in ('sgrouping','smongrouping'):
   digits=list(units(v));assert digits and all(n<=9 for n in digits);s=';'.join(map(str,digits));s+=';0' if digits[-1] else '';return tuple(map(ord,s))
  if field=='snativedigits':return tuple(ch for j in range(u16(strings+2*v)) for ch in array(v,j))
  if field in ('stimeformat','sshortdate','slongdate','syearmonth','sduration','sshorttime','smonthday','sdayname','sabbrevdayname','sshortestdayname','smonthname','sabbrevmonthname'):return array(v,i)
  return units(v)
 rows.append([value(spec[k][1],spec[k][2]) if spec[k][0]==0 else intern(value(spec[k][1],spec[k][2])) for k in keys])
names=[]
for i in range(u16(base+0x28)):
 pos,idx,lcid=struct.unpack_from('<HHI',r,index+8*i);name=''.join(map(chr,units(pos)));assert name.isascii() and len(name)<85 and idx<count;names.append((name,idx,lcid))
text='/* Generated pinned Wine 10.0 locale information, LGPL-2.1-or-later.\n See vendor/wine-nls/LICENSE and config/encoding-sources.json. */\nstatic const uint16_t info_strings[]={\n'+''.join(' '+','.join(map(str,pool[i:i+16]))+',\n' for i in range(0,len(pool),16))+'};\nstatic const struct InfoField info_fields[]={\n'+''.join(' {0x%x,%d},\n'%(k,spec[k][0]) for k in keys)+'};\nstatic const uint32_t info_values[][%d]={\n'%len(keys)+''.join(' {'+','.join(map(str,row))+'},\n' for row in rows)+'};\nstatic const struct InfoName info_names[]={\n'+''.join(' {"%s",%d,0x%x},\n'%v for v in names)+'};\n'
p=R/'port/core/nls_info.inc'
if a.check:assert p.read_text()==text,'Run scripts/gen-nls-info.py'
else:p.write_text(text)
print('PASS: %d locale profiles, %d names, %d information fields including genitive variants, %d UTF-16 data units'%(len(rows),len(names),len(keys),len(pool)))
