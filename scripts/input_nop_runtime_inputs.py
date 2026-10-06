"""Integrate the reached three-store input startup NOP region."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];OUT=R/'generated/integration-review/input-nop';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text());assert meta['passed'] and test['passed'] and test['cases']==256 and test['preparation_sha256']==sha(OUT/'preparation.json') and test['script_sha256']==sha(R/'scripts/test-input-nop.py')
 for n,h in meta['inputs_sha256'].items():assert sha(R/n)==h,n
 assert sha(OUT/'engine.ll')==meta['outputs_sha256']['engine.ll']
 original=engine.read_text();variant=(OUT/'engine.ll').read_text();wrappers=['declare i32 @qol_input_nop_route_ready()'];names=[]
 for row in meta['records']:
  name=row['name'];names.append(name);body=definition(original,name);assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256'];original=original.replace(body,body.replace('@'+name+'(','@tribes_before_input_nop_'+name+'(',1),1);variant=variant.replace('@'+name+'(','@tribes_input_nop_'+name+'(')
  wrappers.append('''declare hidden fastcc void @tribes_input_nop_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_input_nop_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_input_nop_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_input_nop_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
 engine.write_text(original+'\n'+'\n'.join(wrappers));path=out/'input-nop-engine.ll';path.write_text(variant)
 body=definition(dll,'loc_100550B9');changed=body
 for index,(w,(bits,value,pointer)) in enumerate(zip(meta['writes'],[(32,'2425393296','6'),(32,'2425393296','10'),(8,'%15','14')])):
  needle='store i%d %s, i%d* %%%s, align 1'%(bits,value,bits,pointer);assert changed.count(needle)==1
  cast='%%input_nop_pointer_%d = bitcast i%d* %%%s to i8*\n'%(index,bits,pointer) if bits!=8 else ''
  ptr='%%input_nop_pointer_%d'%index if bits!=8 else '%'+pointer
  cast+='%%input_nop_value_%d = zext i%d %s to i64\n'%(index,bits,value)
  changed=changed.replace(needle,cast+'call void @qol_input_nop_write(%%_cpu* %%cpu, i32 %d, i8* %s, i32 %d, i64 %%input_nop_value_%d)'%(w['pc'],ptr,w['size'],index),1)
 dll=dll.replace(body,changed,1)+'\ndeclare void @qol_input_nop_write(%_cpu*, i32, i8*, i32, i64)\n'
 include=out/'input_nop_publication.inc';include.write_text('static const TribesCodeWrite input_nop_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in meta['writes'])+'};\n')
 report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(include),functions=names,writer=dict(name='loc_100550B9',before=hashlib.sha256(body.encode()).hexdigest(),after=hashlib.sha256(changed.encode()).hexdigest()),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'input-nop-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
