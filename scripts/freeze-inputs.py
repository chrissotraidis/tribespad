#!/usr/bin/env python3
"""Freeze acquired inputs. Never infer distribution permission from freeware status."""
import datetime, json, pathlib, sys
from audit_inputs import digest
ROOT=pathlib.Path(__file__).resolve().parents[1]
urls={
 'tribes2gsi.exe':'https://depot.tribes2.net/legacy/tribes2gsi.exe',
 'TribesNEXT_20250922_preview.exe':'https://www.tribesnext.com/files/TribesNEXT_20250922_preview.exe',
 'TribesNext_rc2a.exe':'https://www.tribesnext.com/files/TribesNext_rc2a.exe',
 'yHDTextures2.0.vl2':'https://ln.sync.com/dl/98f4f3a70#sm8cjdcb-whgenvfx-izdxzcya-2fze3ref'}
files=[]
for name,url in urls.items():
 p=ROOT/'ref/downloads'/name
 if not p.is_file(): raise SystemExit('Missing required input: '+name)
 files.append({'name':name,'url':url,'sha256':digest(p),'bytes':p.stat().st_size,'acquired_at':datetime.datetime.fromtimestamp(p.stat().st_mtime,datetime.timezone.utc).isoformat(),'path':str(p.relative_to(ROOT)),'rights':'Local personal port inputs; redistribution authorization has not been established.'})
modules=json.loads((ROOT/'generated/inputs-audit.json').read_text())['modules']
manifest={'schema':1,'graphics_profile':'PlayT2 HD Textures 2.0 (Reworked), yHDTextures2.0.vl2','files':files,'installations':{'diagnostic':'ref/control','product':'ref/product','qol_extracted':'ref/qol'},'modules':[{k:m[k] for k in ['path','sha256','bytes','version','imports','signature_present','signature_verified']} for m in modules],'reference_verified':False}
p=ROOT/'inputs-manifest.json'
if p.exists():
 old=json.loads(p.read_text());expected={f['name']:f['sha256'] for f in old['files']}
 for f in files:
  if expected.get(f['name'])!=f['sha256']: raise SystemExit('Input changed unexpectedly: '+f['name'])
p.write_text(json.dumps(manifest,indent=2)+'\n')
print('Frozen',len(files),'downloads and',len(modules),'PE modules')
