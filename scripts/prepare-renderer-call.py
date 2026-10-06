#!/usr/bin/env python3
"""Prepare the observed single renderer CALL destination rewrite, revision22."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-call';out.mkdir(exist_ok=True);sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
cap=R/'generated/integration-review/gl-query-init/execution/next-stop.json';d=json.loads(cap.read_text());report=cap.with_name('full-result.json');run=json.loads(report.read_text())
assert run['binary_sha256']=='c357c5d416c7f3a637fe374e6a5e27de606296cb26fbc72e0afaa809f720b87d'
assert d['writer_pc']=='0x10047071' and d['code_at_stop']=='e8f0adfcff84' and d['value']=='0xfb408b0' and d['protection_return']==1
engine=R/'generated/integration-review/string-patched-engine/va/engine.ll';s=engine.read_text();body=definition(s,'loc_505D13');assert body.count('@loc_4D0B40(')==1
changed=body.replace('@loc_4D0B40(','@loc_10046600(');assert 'store i32 5266768,' in changed
pre=s[:s.index('define ')];(out/'engine.ll').write_text(pre+'\ndeclare hidden fastcc void @loc_10046600(%_cpu*)\n'+changed)
# Isolate the exact changed CALL tail. Its preceding IR is byte-for-byte identical.
tail=changed[changed.rindex(';    sub esp, esp, 4'):];tail=re.sub(r'%(\d+)',r'%v\1',tail)
assert set(re.findall(r'%v\d+',tail))-set(re.findall(r'(%v\d+) =',tail))=={'%v89'}
cpu=re.search(r'^%_cpu = type .*$',s,re.M)[0]
(out/'call-tail.ll').write_text(cpu+'\ndeclare hidden fastcc void @loc_10046600(%_cpu*)\ndefine void @tested_call(%_cpu* %cpu) {\n %basep = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13\n %v89 = load i64, i64* %basep, align 8\n'+tail.replace('musttail call','call'))
meta=dict(passed=True,entry='loc_505D13',old_target='loc_4D0B40',target='loc_10046600',before_sha256=hashlib.sha256(body.encode()).hexdigest(),after_sha256=hashlib.sha256(changed.encode()).hexdigest(),write=dict(pc=0x10047071,address=0x505d4c,size=4,before=0xfffcadf0,value=0x0fb408b0),inputs_sha256={str(p.relative_to(R)):sha(p) for p in [cap,report,engine,pathlib.Path(__file__).resolve()]},outputs_sha256={n:sha(out/n) for n in ['engine.ll','call-tail.ll']},scope='One changed terminal CALL destination, identical prefix and original return address; captured original protection success and attempted store. No generated executable memory.')
(out/'preparation.json').write_text(json.dumps(meta,indent=2)+'\n');print('Prepared one renderer CALL destination')
