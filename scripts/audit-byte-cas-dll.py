#!/usr/bin/env python3
"""Prove the full DLL differs only at the reached byte atomic block/declaration."""
import argparse,hashlib,json,pathlib,re
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();new=a.lift.resolve()/'native-diagnostic';old=R/'generated/srw/run-20261003T150627-ifc22/native-diagnostic';out=R/'generated/integration-review/locked-cmpxchg-byte'
test=json.loads((out/'result.json').read_text());assert test['passed'] and test['differential_cases']==75776
for n,h in test['source_sha256'].items():assert sha(R/n)==h,n
nm=json.loads((new/'result.json').read_text());om=json.loads((old/'result.json').read_text());assert nm['state']==om['state']=='compiled'
for folder,m in [(new,nm),(old,om)]:assert sha(folder/'module.ll')==m['module_ll_sha256'] and sha(folder/'module.o')==m['object_sha256']
for key in ['imports','exports','tls','entry','image_sha256']:assert nm[key]==om[key],key
assert (new/'dispatch.ll').read_bytes()==(old/'dispatch.ll').read_bytes()
before=(old/'module.ll').read_text();after=(new/'module.ll').read_text();pat=r'^define hidden fastcc void @(loc_[0-9A-F]+)\([^\n]*\n.*?^}\n'
b={m[1]:m[0] for m in re.finditer(pat,before,re.M|re.S)};n={m[1]:m[0] for m in re.finditer(pat,after,re.M|re.S)};assert b.keys()==n.keys()
changed=[k for k in b if b[k]!=n[k]];assert changed==['loc_1007FF5C'],changed
assert 'call void @halopad_trap_unimplemented(i32 268959584)' in b[changed[0]]
assert 'halopad_trap_unimplemented' not in n[changed[0]] and 'call void @tb_locked_cmpxchg8(%_cpu* %cpu, i32 %33, i32 1)' in n[changed[0]]
# Strip the sole new helper declaration and changed function, then require
# byte-for-byte equality of every other function, datum and declaration.
x=before.replace(b[changed[0]],'');y=after.replace(n[changed[0]],'');lines=y.splitlines(True);extra=[l for l in lines if l.startswith('declare ') and '@tb_locked_cmpxchg8(' in l];assert len(extra)==1;y=y.replace(extra[0],'');assert x==y
report=dict(passed=True,definitions=len(b),changed=changed,old_traps=before.count('call void @halopad_trap_unimplemented('),new_traps=after.count('call void @halopad_trap_unimplemented('),test_sha256=sha(out/'result.json'),sources_sha256={str(p.relative_to(R)):sha(p) for p in [old/'module.ll',new/'module.ll',new/'module.o',new/'dispatch.ll',R/'scripts/patches/srw-tribes.patch',R/'toolchains.lock.json',pathlib.Path(__file__).resolve()]},scope='Exactly one original block changes to the independently tested byte atomic; all other emitted text identical after removing its single helper declaration. Remaining diagnostic traps stay explicit. No gameplay acceptance.')
(out/'dll-audit.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(b),'definitions, one changed block; traps',report['old_traps'],'->',report['new_traps'])
