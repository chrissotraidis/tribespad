#!/usr/bin/env python3
"""Reject corrupted or writable constant pins without mutating repository inputs."""
import hashlib,importlib.util,json,pathlib
import pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('routes',R/'scripts/prepare-qol-zlib-routes.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
out=R/'generated/qol-route-constant-tests';out.mkdir(exist_ok=True)
loads=json.loads;image=pefile.PE(str(R/'ref/product/IFC22.dll'));cases=[]
for kind in ['hash','bytes','zero-length','writable']:
 def altered(text,*args,**kwargs):
  value=loads(text,*args,**kwargs)
  if isinstance(value,dict) and value.get('module')=='IFC22.dll' and 'routes' in value:
   c=next(row for row in value['routes'] if row['binding']=='QoL_CRC32FoldFinal')['constant_ranges'][0]
   if kind=='hash':c['sha256']='0'*64
   elif kind=='bytes':c['bytes_hex']='00'*80
   elif kind=='zero-length':c['length']=0
   else:
    c['guest_va']=0x101d4000;data=image.get_data(0x1d4000,80);c['bytes_hex']=data.hex();c['sha256']=hashlib.sha256(data).hexdigest()
   (out/(kind+'-config.json')).write_text(json.dumps(value,indent=2)+'\n')
  return value
 m.json.loads=altered
 try:
  try:m.prepare(out/kind)
  except AssertionError:cases.append(dict(kind=kind,rejected=True))
  else:raise RuntimeError('accepted invalid constant pin: '+kind)
 finally:m.json.loads=loads
paths=[pathlib.Path(__file__).resolve(),R/'scripts/prepare-qol-zlib-routes.py',R/'port/llasm-runtime/qol-zlib-routes.json']
r={'pass':True,'cases':cases,'source_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'scope':'Preparation rejects corrupted hash/bytes,zero-length and writable constant-range metadata. In-process input overlays only; repository route manifest untouched. Not runtime write protection or full module proof.'}
(out/'result.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: four invalid constant-pin fixtures rejected')
