#!/usr/bin/env python3
"""Require an exact one-block DLL change and the independently tested lowering."""
import argparse,hashlib,json,pathlib,re
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args();new=a.lift.resolve()/'native-diagnostic';old=R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic'
out=R/'generated/integration-review/highbyte-compare-repair';testroot=out/'test';test=json.loads((testroot/'result.json').read_text());assert test['passed'] and test['captured_native_case'] and len(test['negative'])==6
for path,h in test['sources_sha256'].items():assert sha(R/path)==h,path
for path,h in test['outputs_sha256'].items():assert sha(testroot/path)==h,path
nm=json.loads((new/'result.json').read_text());om=json.loads((old/'result.json').read_text());identity=json.loads((new.parent/'result.json').read_text())
assert nm['state']=='compiled' and nm['exit']==0 and sha(new/'module.ll')==nm['module_ll_sha256'] and sha(new/'module.o')==nm['object_sha256']
assert om['state']=='compiled' and sha(old/'module.ll')==om['module_ll_sha256']
assert nm['srw_sha256']==identity['srw_sha256']==test['srw_sha256']
assert nm['image_sha256']==om['image_sha256']==test['original_sha256']
for key in ['imports','exports','tls','entry','support_sha256']:assert nm[key]==om[key],key
assert (new/'dispatch.ll').read_bytes()==(old/'dispatch.ll').read_bytes()
before=(old/'module.ll').read_text();after=(new/'module.ll').read_text();pattern=r'^define hidden fastcc void @loc_10115878\([^\n]*\n.*?^}\n'
b=re.search(pattern,before,re.M|re.S);n=re.search(pattern,after,re.M|re.S);assert b and n
assert before[:b.start()]+before[b.end():]==after[:n.start()]+after[n.end():], 'Unrelated generated code changed'
assert 'call void @halopad_trap_unimplemented(i32 269572216)' in b[0]
assert 'halopad_trap_unimplemented' not in n[0]
tail=';tcall loc_1011587A\nmusttail call fastcc void @loc_1011587A(%_cpu* %cpu) nounwind\nret void\n}\n'
assert b[0].endswith(tail) and n[0].endswith(tail)
fixture=(testroot/'bodies.ll').read_text();f=re.search(r'^define hidden fastcc void @compare_0\([^\n]*\n.*?^}\n',fixture,re.M|re.S);assert f
new_instructions=n[0].split('\n',1)[1][:-len(tail)]
fixture_instructions=f[0].split('\n',1)[1];assert fixture_instructions.endswith('ret void\n}\n');fixture_instructions=fixture_instructions[:-len('ret void\n}\n')]
assert new_instructions==fixture_instructions, 'Actual DLL body differs from tested instruction lowering'
old_traps=before.count('call void @halopad_trap_unimplemented(');new_traps=after.count('call void @halopad_trap_unimplemented(');assert old_traps-new_traps==1
report=dict(passed=True,old_native=str(old.relative_to(R)),new_native=str(new.relative_to(R)),changed_definitions=['loc_10115878'],all_other_module_text_identical=True,dispatch_identical=True,tested_body_identical=True,
    old_trap_calls=old_traps,new_trap_calls=new_traps,old_module_sha256=sha(old/'module.ll'),new_module_sha256=sha(new/'module.ll'),new_object_sha256=sha(new/'module.o'),test_sha256=sha(testroot/'result.json'),
    old_preparation_sha256=sha(old/'result.json'),new_preparation_sha256=sha(new/'result.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),
    scope='Fresh full DLL lift changes exactly the formerly trapped CMP block; its emitted instruction body equals the exhaustively tested fixture. All other module text, dispatch, imports, exports, TLS and support identities match the prior accepted module. Diagnostic traps remain elsewhere; integrated continuation remains a separate acceptance.')
(out/'dll-audit.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: one DLL block repaired; all other emitted text and dispatch identical;',new_traps,'diagnostic trap calls remain')
