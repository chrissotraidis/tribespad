"""Bind the two verified NOP-patched entries to their exact original stores."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];OUT=R/'generated/integration-review/post-video-tail-candidate';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text())
 assert meta['passed'] and test['passed'] and not meta['published'] and not test['published'] and test['definitions']==3 and test['cases']==768
 assert test['preparation_sha256']==sha(OUT/'preparation.json')
 for report,key in [(meta,'inputs_sha256'),(test,'sources_sha256')]:
  for n,h in report[key].items():assert sha(R/n)==h,n
 for report in [meta,test]:
  for n,h in report['outputs_sha256'].items():assert sha(OUT/n)==h,n
 original=engine.read_text();variant=(OUT/'engine.ll').read_text();wrappers=['declare i32 @qol_post_video_tail_route_ready()']
 for row in meta['records']:
  name=row['name'];body=definition(original,name);assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256']
  original=original.replace(body,body.replace('@'+name+'(','@tribes_before_post_video_tail_'+name+'(',1),1)
  variant=variant.replace('@'+name+'(', '@tribes_post_video_tail_'+name+'(')
  wrappers.append('''declare hidden fastcc void @tribes_post_video_tail_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_post_video_tail_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_post_video_tail_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_post_video_tail_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
 engine.write_text(original+'\n'+'\n'.join(wrappers));variant_path=out/'post-video-tail-engine.ll';variant_path.write_text(variant)
 records=[];writers=[]
 for row in meta['records']:
  w=row['write'];pc=w['pc'];name='loc_%X'%pc;body=definition(dll,name)
  if pc==0x1005b4bb:
   needle='store i32 %2, i32* %9, align 1\n'
   replacement='%%tail_pointer = bitcast i32* %%9 to i8*\n%%tail_value = zext i32 %%2 to i64\ncall void @qol_post_video_tail_write(%%_cpu* %%cpu, i32 %d, i8* %%tail_pointer, i32 4, i64 %%tail_value)\n'%pc
  else:
   value=int.from_bytes(bytes.fromhex(w['after']),'little');assert w['size']==1 and pc in [0x1005b4d0,0x1005b4ea]
   needle='call void @x86_mov_mem_reg_8(i32 %d, i32 %d) nounwind\n'%(w['address'],value)
   replacement='%%tail_offset = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 13\n%%tail_base = load i64, i64* %%tail_offset, align 8\n%%tail_address = add i64 %%tail_base, %d\n%%tail_pointer = inttoptr i64 %%tail_address to i8*\ncall void @qol_post_video_tail_write(%%_cpu* %%cpu, i32 %d, i8* %%tail_pointer, i32 1, i64 %d)\n'%(w['address'],pc,value)
  assert body.count(needle)==1;changed=body.replace(needle,replacement,1);dll=dll.replace(body,changed,1);writers.append(dict(name=name,original_sha256=hashlib.sha256(body.encode()).hexdigest(),adapted_sha256=hashlib.sha256(changed.encode()).hexdigest()))
  records.append('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(pc,w['address'],w['size'],int.from_bytes(bytes.fromhex(w['before']),'little'),int.from_bytes(bytes.fromhex(w['after']),'little')))
 dll+='\ndeclare void @qol_post_video_tail_write(%_cpu*, i32, i8*, i32, i64)\n'
 include=out/'post_video_tail_publication.inc';include.write_text('static const TribesCodeWrite post_video_tail_writes[]={'+','.join(records)+'};\n')
 report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(variant_path),engine_sha256=sha(engine),include_sha256=sha(include),writers=writers,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Three exact stores activate the verified CALL and PUSH blocks after the final byte store, before the following bank registration. Both intervening VirtualProtect calls and their original conditional branches remain real. First writer retains its original register value and pointer. Unrelated writes remain forbidden; pending bank entries unchanged.')
 (out/'post-video-tail-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,variant_path,report
