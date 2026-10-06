"""Connect the captured seven-hook audio installer to finite native routes and stores."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/audio-hook-bank';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 plan=json.loads((P/'plan.json').read_text());bank=json.loads((P/'result.json').read_text());test=json.loads((P/'test-result.json').read_text());cont=json.loads((P/'continuations/result.json').read_text())
 assert plan['relocation']['delta']==576 and plan['relocation']['renderer_plan_sha256']==sha(R/'generated/integration-review/renderer-hook-bank/plan.json')
 assert plan['verified'] and len(plan['code_writes'])==21 and test['passed'] and test['cases']==1792
 assert bank['plan_sha256']==cont['plan_sha256']==test['plan_sha256']==sha(P/'plan.json')
 assert test['script_sha256']==sha(R/'scripts/test-audio-hook-bank.py') and test['module_sha256']==bank['module_sha256']
 assert bank['module_sha256']==sha(P/'module.ll') and cont['module_sha256']==sha(P/'continuations/module.ll')
 assert plan['script_sha256']==sha(R/'scripts/prepare-audio-hook-bank.py') and bank['script_sha256']==sha(R/'scripts/compile-audio-hook-bank.py') and cont['script_sha256']==sha(R/'scripts/prepare-audio-hook-continuations.py')
 for n,h in cont['source_sha256'].items():assert sha(R/n)==h,n
 entries=json.loads((P/'dispatch-entries.json').read_text());assert sha(P/'dispatch-entries.json')==bank['dispatch_entries_sha256']
 tables=json.loads((R/'generated/integration-review/audio-tables/preparation.json').read_text())
 assert all(not(w['address']<r['continuation'] and r['original_target']<w['address']+w['size']) for w in tables['writes'] for r in plan['trampolines'])
 original=engine.read_text();wrappers=['declare i32 @qol_audio_hooks_route_ready()'];records=[]
 allnames=set(re.findall(r'^define (?:hidden|protected) fastcc void @(loc_[0-9A-F]+)\(',original,re.M))
 for row in plan['trampolines']:
  name='loc_%X'%row['original_target'];target='loc_%X'%row['detour_target'];body=definition(original,name)
  assert not [n for n in allnames if row['original_target']<int(n[4:],16)<row['continuation']],name
  original=original.replace(body,body.replace('@'+name+'(','@tribes_before_audio_hooks_'+name+'(',1),1)
  if not re.search(r'(?:declare|define) (?:hidden )?fastcc void @'+target+r'\(',original):wrappers.append('declare hidden fastcc void @%s(%%_cpu*)'%target)
  wrappers.append('''define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_audio_hooks_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_audio_hooks_%s(%%_cpu* %%cpu)
 ret void
}'''%(name,target,name))
  records.append(dict(name=name,target=target,before_sha256=hashlib.sha256(body.encode()).hexdigest()))
 engine.write_text(original+'\n'+'\n'.join(wrappers)+'\n')
 ct=json.loads((P/'continuations/test-result.json').read_text());assert ct['passed'] and ct['cases']==448 and ct['module_sha256']==cont['module_sha256'] and ct['script_sha256']==sha(R/'scripts/test-audio-hook-continuations.py')
 sources=[];added=[]
 for label,path in [('bank',P/'module.ll'),('continuations',P/'continuations/module.ll')]:
  text=path.read_text();names=re.findall(r'^define (?:hidden|protected) fastcc void @(\w+)\(',text,re.M);gates=['declare i32 @qol_audio_hooks_route_ready()','declare void @qol_audio_hooks_unavailable() noreturn']
  for name in names:
   assert name.startswith('loc_') or name.startswith('tbpatch_'),name
   body=definition(text,name);text=text.replace(body,body.replace('@'+name+'(','@tribes_audio_hooks_'+name+'(',1),1)
   gate='qol_audio_hooks_route_ready'
   gates.append(('''define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_audio_hooks_route_ready()
 %%active = icmp ne i32 %%ready, 0
 br i1 %%active, label %%run, label %%stop
run:
 musttail call fastcc void @tribes_audio_hooks_%s(%%_cpu* %%cpu)
 ret void
stop:
 call void @qol_audio_hooks_unavailable()
 unreachable
}'''%(name,name)).replace('@qol_audio_hooks_route_ready()', '@'+gate+'()'))
  target=out/('audio-hooks-'+label+'.ll');target.write_text(text+'\n'+'\n'.join(gates)+'\n');sources.append(target)
  if label=='continuations':added+=names
 # Keep bridges separate from bank's declarations; each tail transfer preserves CPU.
 bridges=[]
 for v in bank['external_targets']:
  bridges += ['declare hidden fastcc void @loc_%X(ptr)'%v,'define hidden fastcc void @tbpatch_target_%08x(ptr %%cpu) { musttail call fastcc void @loc_%X(ptr %%cpu)\n ret void\n}'%(v,v)]
 path=out/'audio-hooks-bridges.ll';path.write_text('\n'.join(bridges)+'\n');sources.append(path)
 lines=['static const TribesCodeWrite audio_hook_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in plan['code_writes'])+'};']
 ordered=sorted(plan['trampolines'],key=lambda r:r['address'])
 for i,r in enumerate(ordered):lines.append('static const unsigned char audio_bank_%d[]={%s};'%(i,','.join(map(str,bytes.fromhex(r['code_hex'])))))
 lines.append('static const TribesPublishedCodeRange audio_bank_ranges[]={'+','.join('{%du,%du,audio_bank_%d}'%(r['address'],len(bytes.fromhex(r['code_hex'])),i) for i,r in enumerate(ordered))+'};')
 lines.append('static const uint32_t audio_hook_flushes[][2]={'+','.join('{%du,%du}'%(r['original_target'],r['moved_code_bytes']) for r in plan['trampolines'])+',{0x3c0000u,65536u}};')
 lines.append('static const uint32_t audio_hook_callbacks[][2]={'+','.join('{%du,%du}'%(r['callback_slot'],r['address']) for r in plan['trampolines'])+'};')
 inc=out/'audio_hook_publication.inc';inc.write_text('\n'.join(lines)+'\n')
 report=dict(continuation_test_sha256=sha(P/'continuations/test-result.json'),plan_sha256=sha(P/'plan.json'),bank_test_sha256=sha(P/'test-result.json'),functions=records,added=added,bank_entries=entries,script_sha256=sha(pathlib.Path(__file__).resolve()),include_sha256=sha(inc),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources})
 (out/'audio-hook-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,sources,report
