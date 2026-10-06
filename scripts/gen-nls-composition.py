#!/usr/bin/env python3
"""Extract BMP composition pairs from the pinned Wine normalization table."""
import argparse,hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args()
m=json.loads((R/'config/encoding-sources.json').read_text())['composition_reference'];raw=(R/m['local_path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==m['sha256'];w=struct.unpack('<%dH'%(len(raw)//2),raw);size=w[0x30//2];index=w[0x40//2];seq=w[0x42//2];pairs={}
for bucket in range(size):
 at,end=w[index+bucket],w[index+bucket+1]
 while at<end:
  row=[]
  for _ in range(3):
   ch=w[seq+at];at+=1
   if 0xd800<=ch<=0xdbff:
    low=w[seq+at];at+=1;assert 0xdc00<=low<=0xdfff;ch=0x10000+((ch-0xd800)<<10)+(low-0xdc00)
   row.append(ch)
  if max(row)<=65535:
   x,y,z=row;assert (x+95*y)%size==bucket and (x,y) not in pairs;pairs[x,y]=z
 assert at==end
assert pairs[ord('e'),0x301]==0xe9 and pairs[ord('A'),0x30a]==0xc5
text='/* Generated from Wine 10.0 normnfc.nls; LGPL-2.1-or-later.\n   See vendor/wine-nls/COMPOSITION-NOTICE and LICENSE.\n   Source SHA256 '+m['sha256']+' */\nstatic const struct { uint16_t first,second,composed; } nls_composition[]={\n'
text+=''.join('    {0x%04x,0x%04x,0x%04x},\n'%(x,y,z) for (x,y),z in sorted(pairs.items()))+'};\n';p=R/'port/core/nls_composition.inc'
if a.check:assert p.read_text()==text
else:p.write_text(text)
print('PASS: pinned Wine BMP composition pairs:',len(pairs))
