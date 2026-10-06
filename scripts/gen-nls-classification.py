#!/usr/bin/env python3
"""Extract validated compact Wine 10.0 character-type data; no inferred classes."""
import argparse,hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();reference=json.loads((R/'config/encoding-sources.json').read_text())['classification_reference'];raw=(R/reference['local_path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==reference['sha256']
start=struct.unpack_from('<4I',raw)[2];length,index_offset=struct.unpack_from('<2H',raw,start);index_start=start+index_offset+2;assert start+4<=index_start<=start+length<=len(raw);assert (index_start-start-4)%6==0
classes=struct.unpack('<%dH'%((index_start-start-4)//2),raw[start+4:index_start]);index=raw[index_start:start+length]
def word(data,offset):assert 0<=offset<=len(data)-2;return struct.unpack_from('<H',data,offset)[0]
for ch in range(65536):
 level=word(index,2*(ch>>8));slot=word(index,level+2*((ch>>4)&15))+(ch&15);assert 0<=slot<len(index);assert index[slot]*3+2<len(classes)
text='/* Generated from Wine 10.0 sortdefault.nls; SHA256 '+reference['sha256']+'.\n   Wine contributors, LGPL-2.1-or-later. See vendor/wine-nls/LICENSE.\n   Wine-compatible data; not a claim of native Windows or XP-version parity. */\n'
for name,values,ctype,width in [('nls_class_values',classes,'uint16_t',4),('nls_class_index',index,'unsigned char',2)]:
 text+='static const '+ctype+' '+name+'['+str(len(values))+']={\n';text+=''.join('    '+','.join(('0x%0'+str(width)+'x')%v for v in values[i:i+16])+',\n' for i in range(0,len(values),16))+'};\n'
p=R/'port/core/nls_classification.inc'
if a.check:assert p.read_text()==text,'Run scripts/gen-nls-classification.py'
else:p.write_text(text)
print('PASS: pinned compact NLS classification, all 65536 code-unit paths validated, %d bytes index and %d class records'%(len(index),len(classes)//3))
