#!/usr/bin/env python3
"""Verify exact candidate bytes independently and reject corrupted capture evidence."""
import argparse,copy,hashlib,json,pathlib,struct,subprocess,sys
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/qol-patch-capture';negative=out/'negative';negative.mkdir(exist_ok=True);ap=argparse.ArgumentParser();ap.add_argument('--capture',default='generated/qol-initialization/executable-capture-no-sse2.json');a=ap.parse_args();capture=R/a.capture;data=json.loads(capture.read_text());raw=bytearray((R/'ref/control/Tribes2.exe').read_bytes());header=struct.unpack_from('<I',raw,0x3c)[0];assert raw[header:header+4]==b'PE\0\0';count=struct.unpack_from('<H',raw,header+6)[0];optional=header+24;sections=optional+struct.unpack_from('<H',raw,header+20)[0];image_base=struct.unpack_from('<I',raw,optional+28)[0]
for event in data['code_writes']:
 if event['image']!='engine':continue
 rva=event['address']-image_base
 for i in range(count):
  row=sections+40*i;virtual,span,file_offset=struct.unpack_from('<3I',raw,row+12)
  if virtual<=rva and rva+event['size']<=virtual+span:
   offset=file_offset+rva-virtual;before=bytes.fromhex(event['before_bytes']);after=bytes.fromhex(event['observed_bytes']);assert raw[offset:offset+len(before)]==before;raw[offset:offset+len(after)]=after;break
 else:raise AssertionError('capture outside file-backed section')
expected_hash=hashlib.sha256(raw).hexdigest();faults={}
for name in ['engine-preimage','snapshot-hash']:
 bad=copy.deepcopy(data)
 if name=='engine-preimage':
  event=next(e for e in bad['code_writes'] if e['image']=='engine');first=int(event['before_bytes'][:2],16)^255;event['before_bytes']=f'{first:02x}'+event['before_bytes'][2:]
 else:bad['executable_reference']['snapshots'][0]['sha256']='0'*64
 path=negative/(name+'.json');path.write_text(json.dumps(bad)+'\n');run=subprocess.run([sys.executable,str(R/'scripts/build-qol-capture-plan.py'),'--capture',str(path)],capture_output=True,text=True,timeout=30);(negative/(name+'.log')).write_text(run.stdout+run.stderr);assert run.returncode!=0 and 'AssertionError' in run.stderr and json.loads((out/'plan.json').read_text())['state']=='building';faults[name]='rejected before verified-plan publication'
run=subprocess.run([sys.executable,str(R/'scripts/build-qol-capture-plan.py'),'--capture',str(capture.relative_to(R))],capture_output=True,text=True,timeout=30);assert run.returncode==0,run.stderr;plan=json.loads((out/'plan.json').read_text());candidate=R/plan['candidate']['path'];assert candidate.read_bytes()==raw and plan['candidate']['sha256']==expected_hash
report={'state':'finished','pass':True,'driver_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'builder_sha256':hashlib.sha256((R/'scripts/build-qol-capture-plan.py').read_bytes()).hexdigest(),'capture_sha256':hashlib.sha256(capture.read_bytes()).hexdigest(),'candidate_sha256':expected_hash,'faults':faults,'scope':'Independent PE header/section mapping and ordered byte replay, corrupted-input rejection; no native instruction routing/equivalence or game-session claim'};(out/('tests-sse2.json' if data['cpu_profile']['sse2'] else 'tests-no-sse2.json')).write_text(json.dumps(report,indent=2)+'\n');print('PASS: exact independently replayed candidate bytes, corrupted engine preimages and snapshots rejected before verified plan publication')
