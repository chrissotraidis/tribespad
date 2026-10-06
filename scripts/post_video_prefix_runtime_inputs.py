"""Bind the two verified post-video blocks to their exact original stores."""
import hashlib,json,pathlib,re
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];OUT=R/'generated/integration-review/post-video-prefix';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text())
 assert meta['passed'] and test['passed'] and not meta['published'] and not test['published'] and test['definitions']==2 and test['cases']==512
 assert test['preparation_sha256']==sha(OUT/'preparation.json')
 for report,key in [(meta,'inputs_sha256'),(test,'sources_sha256')]:
  for n,h in report[key].items():assert sha(R/n)==h,n
 for report in [meta,test]:
  for n,h in report['outputs_sha256'].items():assert sha(OUT/n)==h,n
 original=engine.read_text();variant=(OUT/'engine.ll').read_text();wrappers=['declare i32 @qol_post_video_prefix_route_ready()']
 for row in meta['records']:
  name=row['name'];body=definition(original,name);assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256']
  original=original.replace(body,body.replace('@'+name+'(','@tribes_before_post_video_'+name+'(',1),1)
  variant=variant.replace('define hidden fastcc void @'+name+'(','define hidden fastcc void @tribes_post_video_'+name+'(',1)
  wrappers.append('''declare hidden fastcc void @tribes_post_video_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_post_video_prefix_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_post_video_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_post_video_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
 engine.write_text(original+'\n'+'\n'.join(wrappers));variant_path=out/'post-video-prefix-engine.ll';variant_path.write_text(variant)
 writers=[]
 for name,pc,address,size,value,needle in [('loc_1005B384',0x1005b384,0x5bb96b,4,0x1004ae80,'store i32 268742272, i32* %5, align 1\n'),('loc_1005B3A1',0x1005b3a1,0x561689,1,0x5b,'call void @x86_mov_mem_reg_8(i32 5641865, i32 91) nounwind\n')]:
  body=definition(dll,name);assert body.count(needle)==1
  replacement='''%%post_video_offset = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 13
%%post_video_base = load i64, i64* %%post_video_offset, align 8
%%post_video_address = add i64 %%post_video_base, %d
%%post_video_pointer = inttoptr i64 %%post_video_address to i8*
call void @qol_post_video_prefix_write(%%_cpu* %%cpu, i32 %d, i8* %%post_video_pointer, i32 %d, i64 %d)
'''%(address,pc,size,value)
  dll=dll.replace(body,body.replace(needle,replacement),1);writers.append(dict(name=name,pc=pc,original_sha256=hashlib.sha256(body.encode()).hexdigest()))
 dll+='\ndeclare void @qol_post_video_prefix_write(%_cpu*, i32, i8*, i32, i64)\n'
 records=[]
 for row in meta['records']:
  w=row['write'];records.append('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],int.from_bytes(bytes.fromhex(w['before']),'little'),int.from_bytes(bytes.fromhex(w['after']),'little')))
 include=out/'post_video_prefix_publication.inc';include.write_text('static const TribesCodeWrite post_video_prefix_writes[]={'+','.join(records)+'};\n')
 report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(variant_path),engine_sha256=sha(engine),include_sha256=sha(include),writers=writers,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Two exact stores activate the verified PUSH/JMP blocks after the second store, before the next engine registration. Intervening VirtualProtect remains real. Partial-phase dispatch and unrelated writes remain forbidden; pending bank entries unchanged.')
 (out/'post-video-prefix-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,variant_path,report
