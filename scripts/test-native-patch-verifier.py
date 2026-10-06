#!/usr/bin/env python3
"""Reject altered images, original-byte preimages and trampoline continuations."""
import argparse,copy,hashlib,importlib.util,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('capture',type=pathlib.Path);a=p.parse_args()
spec=importlib.util.spec_from_file_location('verify_native',R/'scripts/verify-native-patch-capture.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
source=a.capture.resolve();reference=R/'generated/qol-patch-capture/plan.json';positive=v.verify(source,reference)
out=R/'generated/integration-review/native-patch-verifier';out.mkdir(exist_ok=True)
original=json.loads(source.read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();cases=[]
mutations=[('engine-unreviewed','engine-after.bin',0x1000),('qol-unreviewed','qol-after.bin',0x1000),('trampoline-original','patch-after.bin',0xd8+32),('trampoline-continuation','patch-after.bin',0xd8+6+1),('trampoline-target','patch-after.bin',0xd8+68),('snapshot-hash','patch-after.bin',0)]
for t in positive['trampolines']:
 if t['original_target']==0x5c38a0:mutations.append(('relocated-call-target','patch-after.bin',t['address']-positive['native_allocation']['address']+4))
for name,filename,offset in mutations:
 report=copy.deepcopy(original);row=report['patch_snapshots'][filename];data=bytearray((R/row['path']).read_bytes());data[offset]^=1;path=out/(name+'.bin');path.write_bytes(data);row['path']=str(path.relative_to(R))
 if name!='snapshot-hash':row['sha256']=sha(path)
 mutated=out/(name+'.json');mutated.write_text(json.dumps(report))
 try:v.verify(mutated,reference)
 except AssertionError:cases.append(name)
 else:raise AssertionError('Mutation accepted: '+name)
result=dict(passed=True,positive_trampolines=len(positive['trampolines']),rejected=cases,capture_sha256=sha(source),reference_sha256=sha(reference),source_sha256={str(f.relative_to(R)):sha(f) for f in [pathlib.Path(__file__).resolve(),R/'scripts/verify-native-patch-capture.py']})
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS:',len(cases),'native patch capture mutations rejected; positive capture verified')
