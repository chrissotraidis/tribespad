"""Bind the two verified NOP-patched entries to their exact original stores."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];OUT=R/'generated/integration-review/post-video-nop-candidate';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text())
 assert meta['passed'] and test['passed'] and not meta['published'] and not test['published'] and test['definitions']==2 and test['cases']==512
 assert test['preparation_sha256']==sha(OUT/'preparation.json')
 for report,key in [(meta,'inputs_sha256'),(test,'sources_sha256')]:
  for n,h in report[key].items():assert sha(R/n)==h,n
 for report in [meta,test]:
  for n,h in report['outputs_sha256'].items():assert sha(OUT/n)==h,n
 original=engine.read_text();variant=(OUT/'engine.ll').read_text();wrappers=['declare i32 @qol_post_video_nop_route_ready()']
 for row in meta['records']:
  name=row['name'];body=definition(original,name);assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256']
  original=original.replace(body,body.replace('@'+name+'(','@tribes_before_post_video_nop_'+name+'(',1),1)
  variant=variant.replace('@'+name+'(', '@tribes_post_video_nop_'+name+'(')
  wrappers.append('''declare hidden fastcc void @tribes_post_video_nop_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_post_video_nop_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_post_video_nop_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_post_video_nop_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
 engine.write_text(original+'\n'+'\n'.join(wrappers));variant_path=out/'post-video-nop-engine.ll';variant_path.write_text(variant)
 body=definition(dll,'loc_1005B3DA');changed=body;records=[]
 slots=[(32,'2425393296','7'),(32,'2425393296','11'),(32,'2425393296','15'),(32,'2425393296','19'),(32,'2425393296','23'),(32,'2425393296','27'),(16,'%32','31'),(8,'%37','36')]
 assert len(meta['writes'])==len(slots)==8
 for index,(w,(bits,value,pointer)) in enumerate(zip(meta['writes'],slots)):
  assert w['size']*8==bits and w['address']==0x4025e5+sum(x['size'] for x in meta['writes'][:index])
  needle='store i%d %s, i%d* %%%s, align 1\n'%(bits,value,bits,pointer);assert changed.count(needle)==1
  cast='%%nop_pointer_%d = bitcast i%d* %%%s to i8*\n'%(index,bits,pointer) if bits!=8 else ''
  ptr='%%nop_pointer_%d'%index if bits!=8 else '%'+pointer
  cast+='%%nop_value_%d = zext i%d %s to i64\n'%(index,bits,value)
  changed=changed.replace(needle,cast+'call void @qol_post_video_nop_write(%%_cpu* %%cpu, i32 %d, i8* %s, i32 %d, i64 %%nop_value_%d)\n'%(w['pc'],ptr,w['size'],index),1)
  records.append('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],int.from_bytes(bytes.fromhex(w['before']),'little'),w['value']&((1<<bits)-1)))
 dll=dll.replace(body,changed,1)+'\ndeclare void @qol_post_video_nop_write(%_cpu*, i32, i8*, i32, i64)\n'
 writers=[dict(name='loc_1005B3DA',original_sha256=hashlib.sha256(body.encode()).hexdigest(),adapted_sha256=hashlib.sha256(changed.encode()).hexdigest())]
 include=out/'post_video_nop_publication.inc';include.write_text('static const TribesCodeWrite post_video_nop_writes[]={'+','.join(records)+'};\n')
 report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(variant_path),engine_sha256=sha(engine),include_sha256=sha(include),writers=writers,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Eight exact original stores activate both verified NOP entries after the final byte store, before the next registration. Width-truncated original values and pointers are retained. Unrelated writes remain forbidden; pending bank entries unchanged.')
 (out/'post-video-nop-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,variant_path,report
