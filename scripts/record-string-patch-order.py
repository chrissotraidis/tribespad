#!/usr/bin/env python3
"""Replay the frozen seventh-patch reference and record intermediate flush bytes."""
import hashlib,json,pathlib,sys
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
frozen=R/'generated/integration-review/http-publication-checkpoint-20261003'
trace=frozen/'next-reference/result.json';reference=json.loads(trace.read_text())
script=frozen/'sources/scripts/trace-string-qol-patches.py'
assert sha(script)==reference['script_sha256']
text=script.read_text();needle=" effect=cache.invalidate(stop['address'],stop['size']);services.append("
root_expression='R=pathlib.Path(__file__).resolve().parents[1]'
assert text.count(root_expression)==1
text=text.replace(root_expression,'R=repository_root')
assert text.count(needle)==1
text=text.replace(needle," flush_order.append(dict(return_pc=stop['return_pc'],address=stop['address'],size=stop['size'],bytes_hex=bytes(u.mem_read(stop['address'],stop['size'])).hex(),engine_writes=sum(e['category']=='code' and e['image']=='engine' for e in writes),recorded_writes=len(writes)))\n"+needle)
out=R/'generated/integration-review/string-patch-order';out.mkdir(exist_ok=True)
sys.argv=[str(script),str(frozen/'native-proof/result.json'),'--output',str(out)]
namespace={'__file__':str(script),'__name__':'__main__','flush_order':[],'repository_root':R}
exec(compile(text,str(script),'exec'),namespace)
replayed=json.loads((out/'result.json').read_text())
def normalized(value):
 value=json.loads(json.dumps(value))
 for row in value['changed_regions']:row.pop('after_path')
 return value
assert normalized(replayed)==normalized(reference),'Replay differs from frozen reference'
flushes=namespace['flush_order'];assert len(flushes)==12
report=dict(passed=True,trace_sha256=sha(trace),checkpoint_sha256=reference['checkpoint_sha256'],reference_script_sha256=sha(script),replayed_trace_sha256=sha(out/'result.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),flushes=flushes,scope='Frozen original x86 reference replay unchanged except output paths. Additional observations record exact intermediate flush bytes and completed engine-write counts. No guest state edits or native activation.')
(out/'order.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: unchanged reference replay;12 intermediate flushes:',[r['engine_writes'] for r in flushes])
