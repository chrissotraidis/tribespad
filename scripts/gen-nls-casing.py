#!/usr/bin/env python3
"""Extract BMP default case maps from pinned Wine 10.0 NLS data."""
import argparse,hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();ref=json.loads((R/'config/encoding-sources.json').read_text())['classification_reference'];raw=(R/ref['local_path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==ref['sha256']
base=struct.unpack_from('<4I',raw)[1];upper=base+4;lower=upper+2*struct.unpack_from('<H',raw,base+2)[0]
text='/* Generated from Wine 10.0 sortdefault.nls; SHA256 '+ref['sha256']+'.\n   Default BMP case maps only. Wine contributors, LGPL-2.1-or-later.\n   See vendor/wine-nls/LICENSE. Native Windows parity is unverified. */\n'
for name,offset in [('nls_upper_case',upper),('nls_lower_case',lower)]:
 maximum=255
 for ch in range(65536):
  a1=struct.unpack_from('<H',raw,offset+2*(ch>>8))[0]+((ch>>4)&15);a2=struct.unpack_from('<H',raw,offset+2*a1)[0]+(ch&15);maximum=max(maximum,a1,a2)
 values=struct.unpack_from('<%dH'%(maximum+1),raw,offset);text+='static const uint16_t '+name+'['+str(len(values))+']={\n'+''.join('    '+','.join('0x%04x'%v for v in values[i:i+16])+',\n' for i in range(0,len(values),16))+'};\n'
p=R/'port/core/nls_casing.inc'
if a.check:assert p.read_text()==text,'Run scripts/gen-nls-casing.py'
else:p.write_text(text)
print('PASS: pinned default NLS case maps, all 65536 BMP lookup paths validated')
