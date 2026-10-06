"""Integrate the bounded five-store original startup CALL sequence."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];OUT=R/'generated/integration-review/startup-calls';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text());assert meta['passed'] and test['passed'] and test['cases']==512 and test['preparation_sha256']==sha(OUT/'preparation.json') and test['script_sha256']==sha(R/'scripts/test-startup-calls.py')
 for n,h in meta['inputs_sha256'].items():assert sha(R/n)==h,n
 assert sha(OUT/'engine.ll')==meta['outputs_sha256']['engine.ll']
 original=engine.read_text();variant=(OUT/'engine.ll').read_text();wrappers=['declare i32 @qol_startup_calls_route_ready()'];names=[]
 for row in meta['records']:
  name=row['name']
  if row.get('added'):continue
  names.append(name);body=definition(original,name);assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256'];original=original.replace(body,body.replace('@'+name+'(','@tribes_before_startup_calls_'+name+'(',1),1);variant=variant.replace('@'+name+'(','@tribes_startup_calls_'+name+'(')
  wrappers.append('''declare hidden fastcc void @tribes_startup_calls_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_startup_calls_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_startup_calls_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_startup_calls_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
 engine.write_text(original+'\n'+'\n'.join(wrappers));path=out/'startup-calls-engine.ll';path.write_text(variant)
 # Keep the actual values/pointers produced by the original instructions.
 writers=[]
 for name,specs in [('loc_10055719',[(0x10055719,0x410c79,1,232),(0x10055720,None,4,None),(0x10055722,0x410c7e,1,144)]),('loc_1005579E',[(0x100557a3,None,4,None)]),('loc_100557F1',[(0x100557f1,None,4,None)])]:
  body=definition(dll,name);changed=body
  for pc,address,size,value in specs:
   tag='startup_%x'%pc
   if size==1:
    needle='call void @x86_mov_mem_reg_8(i32 %d, i32 %d) nounwind'%(address,value)
    replacement='''%%%s_offset = getelementptr %%_cpu, %%_cpu* %%cpu, i32 0, i32 13
%%%s_base = load i64, i64* %%%s_offset, align 8
%%%s_address = add i64 %%%s_base, %d
%%%s_pointer = inttoptr i64 %%%s_address to i8*
call void @qol_startup_calls_write(%%_cpu* %%cpu, i32 %d, i8* %%%s_pointer, i32 1, i64 %d)'''%(tag,tag,tag,tag,tag,address,tag,tag,pc,tag,value)
   else:
    val,ptr=('%3','%8') if pc==0x100557a3 else ('%2','%9');needle='store i32 %s, i32* %s, align 1'%(val,ptr)
    replacement='%%%s_pointer = bitcast i32* %s to i8*\n%%%s_value = zext i32 %s to i64\ncall void @qol_startup_calls_write(%%_cpu* %%cpu, i32 %d, i8* %%%s_pointer, i32 4, i64 %%%s_value)'%(tag,ptr,tag,val,pc,tag,tag)
   assert changed.count(needle)==1,(name,needle);changed=changed.replace(needle,replacement,1)
  dll=dll.replace(body,changed,1);writers.append(dict(name=name,before=hashlib.sha256(body.encode()).hexdigest(),after=hashlib.sha256(changed.encode()).hexdigest()))
 dll+='\ndeclare void @qol_startup_calls_write(%_cpu*, i32, i8*, i32, i64)\n'
 include=out/'startup_calls_publication.inc';include.write_text('static const TribesCodeWrite startup_calls_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in meta['writes'])+'};\n')
 report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(include),functions=names,added=['loc_410C7E'],writers=writers,script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'startup-calls-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
