"""Connect the captured 34-hook installer to finite native routes and stores."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/pending-hook-bank';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 plan=json.loads((P/'plan.json').read_text());bank=json.loads((P/'result.json').read_text());test=json.loads((P/'test-result.json').read_text());cont=json.loads((P/'continuations/result.json').read_text())
 assert plan['verified'] and len(plan['code_writes'])==86 and test['passed'] and test['cases']==4608
 assert bank['plan_sha256']==cont['plan_sha256']==test['plan_sha256']==sha(P/'plan.json')
 assert test['script_sha256']==sha(R/'scripts/test-pending-hook-bank.py') and test['module_sha256']==bank['module_sha256']
 assert bank['module_sha256']==sha(P/'module.ll') and cont['module_sha256']==sha(P/'continuations/module.ll')
 assert plan['script_sha256']==sha(R/'scripts/prepare-pending-hook-bank.py') and bank['script_sha256']==sha(R/'scripts/compile-pending-hook-bank.py') and cont['script_sha256']==sha(R/'scripts/prepare-pending-hook-continuations.py')
 for n,h in cont['source_sha256'].items():assert sha(R/n)==h,n
 entries=json.loads((P/'dispatch-entries.json').read_text());assert sha(P/'dispatch-entries.json')==bank['dispatch_entries_sha256']
 original=engine.read_text();wrappers=['declare i32 @qol_pending_hooks_route_ready()'];records=[]
 allnames=set(re.findall(r'^define (?:hidden|protected) fastcc void @(loc_[0-9A-F]+)\(',original,re.M))
 for row in plan['trampolines']:
  name='loc_%X'%row['original_target'];target='loc_%X'%row['detour_target'];body=definition(original,name)
  assert not [n for n in allnames if row['original_target']<int(n[4:],16)<row['continuation']],name
  original=original.replace(body,body.replace('@'+name+'(','@tribes_before_pending_'+name+'(',1),1)
  if not re.search(r'(?:declare|define) (?:hidden )?fastcc void @'+target+r'\(',original):wrappers.append('declare hidden fastcc void @%s(%%_cpu*)'%target)
  wrappers.append('''define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_pending_hooks_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_pending_%s(%%_cpu* %%cpu)
 ret void
}'''%(name,target,name))
  records.append(dict(name=name,target=target,before_sha256=hashlib.sha256(body.encode()).hexdigest()))
 engine.write_text(original+'\n'+'\n'.join(wrappers)+'\n')
 detour=json.loads((P/'detours/result.json').read_text());sqrt_test=json.loads((P/'sqrtss/result.json').read_text())
 assert detour['passed'] and detour['plan_sha256']==sha(P/'plan.json') and detour['module_sha256']==sha(P/'detours/module.ll') and detour['script_sha256']==sha(R/'scripts/prepare-pending-hook-detours.py')
 for n,h in detour['source_sha256'].items():assert sha(R/n)==h,n
 assert sqrt_test['passed'] and sqrt_test['helper_sha256']==sha(R/'port/llasm-support/tribes_sqrtss.c') and sqrt_test['script_sha256']==sha(R/'scripts/test-pending-sqrtss.py')
 sources=[P/'detours/module.ll',R/'port/llasm-support/tribes_sqrtss.c'];added=[]
 for label,path in [('bank',P/'module.ll'),('continuations',P/'continuations/module.ll')]:
  text=path.read_text();names=re.findall(r'^define (?:hidden|protected) fastcc void @(\w+)\(',text,re.M);gates=['declare i32 @qol_pending_hooks_route_ready()','declare void @qol_pending_hooks_unavailable() noreturn']
  for name in names:
   assert name.startswith('loc_') or name.startswith('tbpatch_'),name
   body=definition(text,name);text=text.replace(body,body.replace('@'+name+'(','@tribes_pending_'+name+'(',1),1)
   gates.append('''define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_pending_hooks_route_ready()
 %%active = icmp ne i32 %%ready, 0
 br i1 %%active, label %%run, label %%stop
run:
 musttail call fastcc void @tribes_pending_%s(%%_cpu* %%cpu)
 ret void
stop:
 call void @qol_pending_hooks_unavailable()
 unreachable
}'''%(name,name))
  target=out/('pending-hooks-'+label+'.ll');target.write_text(text+'\n'+'\n'.join(gates)+'\n');sources.append(target)
  if label=='continuations':added+=names
 # Keep bridges separate from bank's declarations; each tail transfer preserves CPU.
 bridges=[]
 for v in bank['external_targets']:
  bridges += ['declare hidden fastcc void @loc_%X(ptr)'%v,'define hidden fastcc void @tbpatch_target_%08x(ptr %%cpu) { musttail call fastcc void @loc_%X(ptr %%cpu)\n ret void\n}'%(v,v)]
 path=out/'pending-hooks-bridges.ll';path.write_text('\n'.join(bridges)+'\n');sources.append(path)
 body=definition(dll,'loc_10072855');changed=body
 for bits,val,ptr,pc in [(8,'%41','40',0x10072860),(32,'%47','73',0x10072865)]:
  needle='store i%d %s, i%d* %%%s, align 1'%(bits,val,bits,ptr);assert changed.count(needle)==1
  cast='%%pending_ptr_%d = bitcast i%d* %%%s to i8*\n'%(bits,bits,ptr) if bits!=8 else ''
  pointer='%%pending_ptr_%d'%bits if bits!=8 else '%'+ptr
  cast+='%%pending_value_%d = zext i%d %s to i64\n'%(bits,bits,val)
  changed=changed.replace(needle,cast+'call void @qol_pending_hooks_shared_write(%%_cpu* %%cpu, i32 %d, i8* %s, i32 %d, i64 %%pending_value_%d)'%(pc,pointer,bits//8,bits),1)
 dll=dll.replace(body,changed,1)+'\ndeclare void @qol_pending_hooks_shared_write(%_cpu*, i32, i8*, i32, i64)\n'
 lines=['static const TribesCodeWrite pending_hook_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in plan['code_writes'])+'};']
 ordered=sorted(plan['trampolines'],key=lambda r:r['address'])
 for i,r in enumerate(ordered):lines.append('static const unsigned char pending_bank_%d[]={%s};'%(i,','.join(map(str,bytes.fromhex(r['code_hex'])))))
 lines.append('static const TribesPublishedCodeRange pending_bank_ranges[]={'+','.join('{%du,%du,pending_bank_%d}'%(r['address'],len(bytes.fromhex(r['code_hex'])),i) for i,r in enumerate(ordered))+'};')
 lines.append('static const uint32_t pending_hook_flushes[][2]={'+','.join('{%du,%du}'%(r['original_target'],r['moved_code_bytes']) for r in plan['trampolines'])+',{0x3c0000u,65536u}};')
 lines.append('static const uint32_t pending_hook_callbacks[][2]={'+','.join('{%du,%du}'%(r['callback_slot'],r['address']) for r in plan['trampolines'])+'};')
 inc=out/'pending_hook_publication.inc';inc.write_text('\n'.join(lines)+'\n')
 report=dict(detour_entries=detour['added'],detour_metadata_sha256=sha(P/'detours/result.json'),sqrtss_test_sha256=sha(P/'sqrtss/result.json'),plan_sha256=sha(P/'plan.json'),bank_test_sha256=sha(P/'test-result.json'),functions=records,added=added,bank_entries=entries,script_sha256=sha(pathlib.Path(__file__).resolve()),include_sha256=sha(inc),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources})
 (out/'pending-hook-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,sources,report
