"""Bind two dynamic display MOV variants to their original ordered writers."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];OUT=R/'generated/integration-review/display-immediates-candidate';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text());assert meta['passed'] and test['passed'] and not meta['published'] and not test['published'] and test['cases']==512 and test['definitions']==2;assert test['preparation_sha256']==sha(OUT/'preparation.json')
 for report,key in [(meta,'inputs_sha256'),(test,'sources_sha256')]:
  for n,h in report[key].items():assert sha(R/n)==h,n
 for report in [meta,test]:
  for n,h in report['outputs_sha256'].items():assert sha(OUT/n)==h,n
 original=engine.read_text();variant=(OUT/'engine.ll').read_text();wrappers=['declare i32 @qol_display_immediates_route_ready()'];writers=[]
 for row in meta['records']:
  name=row['name'];body=definition(original,name);assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256'];original=original.replace(body,body.replace('@'+name+'(','@tribes_before_display_'+name+'(',1),1);variant=variant.replace('@'+name+'(','@tribes_display_'+name+'(')
  wrappers.append('''declare hidden fastcc void @tribes_display_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_display_immediates_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_display_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_display_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
  pc=row['writer'];writer='loc_%X'%pc;body=definition(dll,writer);needle='call void @x86_mov_mem_reg_32(i32 %d, i32 %%2) nounwind'%row['address'];assert body.count(needle)==1
  replacement='''%display_offset = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13
%display_base = load i64, i64* %display_offset, align 8
%display_address = add i64 %display_base, ADDRESS
%display_pointer = inttoptr i64 %display_address to i8*
%display_value = zext i32 %2 to i64
call void @qol_display_immediates_write(%_cpu* %cpu, i32 WRITER, i8* %display_pointer, i32 4, i64 %display_value)'''.replace('ADDRESS',str(row['address'])).replace('WRITER',str(pc))
  changed=body.replace(needle,replacement,1);dll=dll.replace(body,changed,1);writers.append(dict(name=writer,original_sha256=hashlib.sha256(body.encode()).hexdigest(),adapted_sha256=hashlib.sha256(changed.encode()).hexdigest()))
 dll+='\ndeclare void @qol_display_immediates_write(%_cpu*, i32, i8*, i32, i64)\n';engine.write_text(original+'\n'+'\n'.join(wrappers));p=out/'display-immediates-engine.ll';p.write_text(variant)
 include=out/'display_immediates_publication.inc';include.write_text('static TribesCodeWrite display_immediates_writes[]={'+','.join('{%du,%du,4,%du,0}'%(r['writer'],r['address'],r['before']) for r in meta['records'])+'};\n')
 report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(p),engine_sha256=sha(engine),include_sha256=sha(include),writers=writers,script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Two original stores with per-run values validated from the private native display snapshot. Both must complete in order before the two MOV variants become callable. Ordinary code writes and other values remain forbidden.')
 (out/'display-immediates-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,p,report
