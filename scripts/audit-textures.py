#!/usr/bin/env python3
"""Check HD archive integrity, replacement paths and PNG dimensions without downsampling."""
import collections,hashlib,json,pathlib,struct,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1];p=ROOT/'ref/downloads/yHDTextures2.0.vl2'
expected=next(f['sha256'] for f in json.loads((ROOT/'inputs-manifest.json').read_text())['files'] if f['name']==p.name)
if hashlib.sha256(p.read_bytes()).hexdigest()!=expected:raise SystemExit('HD archive identity changed')
with zipfile.ZipFile(p) as z:
 bad=z.testzip()
 if bad:raise SystemExit('Bad archive CRC: '+bad)
 names=collections.Counter(n.casefold() for n in z.namelist());duplicates=[n for n,c in names.items() if c>1]
 if duplicates:raise SystemExit('Case-colliding HD entries: '+str(duplicates))
 png=[];other=[]
 for i in z.infolist():
  path=pathlib.PurePosixPath(i.filename)
  if path.is_absolute() or '..' in path.parts:raise SystemExit('Unsafe path: '+i.filename)
  if i.is_dir():continue
  if path.suffix.lower()=='.png':
   b=z.read(i)
   if b[:8]!=b'\x89PNG\r\n\x1a\n' or b[12:16]!=b'IHDR':raise SystemExit('Invalid PNG: '+i.filename)
   w,h=struct.unpack('>II',b[16:24]);png.append({'path':i.filename,'width':w,'height':h,'bytes':i.file_size,'sha256':hashlib.sha256(b).hexdigest()})
  else:other.append(i.filename)
 info=z.read('Info.txt').decode(errors='replace')
r={'sha256':expected,'png_count':len(png),'max_dimension':max(max(t['width'],t['height']) for t in png),'above_512':sum(max(t['width'],t['height'])>512 for t in png),'dimensions':dict(collections.Counter(str(t['width'])+'x'+str(t['height']) for t in png)),'other_files':other,'info_text':info,'crc_verified':True,'textures':png,'gpu_sampling_verified':False}
(ROOT/'generated/texture-audit.json').write_text(json.dumps(r,indent=2)+'\n');print(len(png),'PNGs;',r['above_512'],'exceed 512;',r['max_dimension'],'max dimension; CRC and paths verified')
