#!/usr/bin/env python3
"""Pin all CP437 environment byte mappings against Microsoft data and Python."""
import argparse,hashlib,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args()
d=json.loads((R/'config/encoding-sources.json').read_text())['oem_cp437'];raw=(R/d['local_path']).read_bytes();assert hashlib.sha256(raw).hexdigest()==d['sha256'];table={}
for line in raw.decode('ascii').rstrip('\x1a').splitlines():
 parts=line.split('#')[0].split()
 if parts:assert len(parts)==2;byte,unit=[int(x,16) for x in parts];assert byte not in table;table[byte]=unit
assert sorted(table)==list(range(256));values=[table[i] for i in range(256)];assert values==[ord(c) for c in bytes(range(256)).decode('cp437')]
s='/* Generated from Microsoft CP437.TXT by gen-oem-environment-table.py.\n   Source SHA256 '+d['sha256']+'; all 256 entries cross-checked with Python cp437. */\nstatic const uint16_t cp437_environment_to_unicode[256]={\n'
s+=''.join('    '+','.join('0x%04x'%v for v in values[i:i+8])+',\n' for i in range(0,256,8))+'};\n';p=R/'port/core/cp437_environment_table.inc'
if a.check:assert p.read_text()==s,'Run scripts/gen-oem-environment-table.py'
else:p.write_text(s)
print('PASS: 256 pinned primary-source OEM CP437 environment mappings agree with independent Python codec')
