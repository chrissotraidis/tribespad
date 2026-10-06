#!/usr/bin/env python3
"""Download frozen official installers. The encrypted HD share needs a browser download."""
import argparse,hashlib,json,pathlib,shutil,urllib.request
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--hd',type=pathlib.Path,help='Downloaded yHDTextures2.0.vl2');a=p.parse_args()
manifest=json.loads((ROOT/'inputs-manifest.json').read_text())
def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
for item in manifest['files']:
 dst=ROOT/item['path'];dst.parent.mkdir(parents=True,exist_ok=True)
 if dst.exists():
  if sha(dst)!=item['sha256']:raise SystemExit('Unexpected existing input: '+str(dst))
  print('Verified',item['name']);continue
 tmp=dst.with_suffix(dst.suffix+'.partial')
 if item['name'].endswith('.vl2'):
  if not a.hd:print('Browser download required:',item['url']);continue
  shutil.copyfile(a.hd,tmp)
 else:
  with urllib.request.urlopen(item['url'],timeout=30) as response,tmp.open('wb') as out:shutil.copyfileobj(response,out)
 if sha(tmp)!=item['sha256']:raise SystemExit('Downloaded hash mismatch; partial preserved: '+str(tmp))
 tmp.rename(dst);print('Acquired and verified',item['name'])
