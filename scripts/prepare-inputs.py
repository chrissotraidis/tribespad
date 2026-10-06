#!/usr/bin/env python3
"""Extract verified official inputs into isolated diagnostic and product installations."""
import hashlib,json,pathlib,shutil,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
manifest=json.loads((ROOT/'inputs-manifest.json').read_text())
for item in manifest['files']:
 p=ROOT/item['path'];h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 if h.hexdigest()!=item['sha256']:raise SystemExit('Unexpected input: '+item['name'])
rewise=ROOT/'ref/rewise';lock=json.loads((ROOT/'dependencies.lock.json').read_text());source=next(s for s in lock['sources'] if s['name']=='rewise')
if not rewise.exists():subprocess.run(['git','clone',source['url'],str(rewise)],check=True)
revision=subprocess.check_output(['git','-C',str(rewise),'rev-parse','HEAD'],text=True).strip()
if revision!=source['revision']:subprocess.run(['git','-C',str(rewise),'checkout','--detach',source['revision']],check=True)
subprocess.run(['make','CC=clang','CFLAGS=-Wall -O2 -lz'],cwd=rewise,check=True)
base=ROOT/'ref/base';base.mkdir(parents=True,exist_ok=True)
subprocess.run([str(rewise/'rewise'),'-x',str(base),str(ROOT/'ref/downloads/tribes2gsi.exe')],check=True,stdout=subprocess.DEVNULL)
qol=ROOT/'ref/qol'
subprocess.run(['7zz','x','-y','-o'+str(qol),str(ROOT/'ref/downloads/TribesNEXT_20250922_preview.exe')],check=True,stdout=subprocess.DEVNULL)
for name in ['control','product']:
 dst=ROOT/'ref'/name
 if dst.exists():
  print('Preserved existing installation:',dst);continue
 shutil.copytree(base/'MAINDIR/Tribes2/GameData',dst)
 if name=='product':
  shutil.copytree(qol,dst,dirs_exist_ok=True)
  shutil.copy2(ROOT/'ref/downloads/yHDTextures2.0.vl2',dst/'base/yHDTextures2.0.vl2')
exe=ROOT/'ref/control/Tribes2.exe';expected=next(m['sha256'] for m in manifest['modules'] if m['path']=='ref/control/Tribes2.exe')
if hashlib.sha256(exe.read_bytes()).hexdigest()!=expected:raise SystemExit('Extracted executable identity mismatch')
for name in ['control','product']:
 if hashlib.sha256((ROOT/'ref'/name/'Tribes2.exe').read_bytes()).hexdigest()!=expected:raise SystemExit('Unexpected installed executable: '+name)
for module in manifest['modules']:
 if module['path'].startswith('ref/qol/'):
  filename=pathlib.Path(module['path']).name
  for p in [ROOT/module['path'],ROOT/'ref/product'/filename]:
   if hashlib.sha256(p.read_bytes()).hexdigest()!=module['sha256']:raise SystemExit('Unexpected patch module: '+str(p))
print('Verified original build 25034 input; reference gameplay has not been established.')
