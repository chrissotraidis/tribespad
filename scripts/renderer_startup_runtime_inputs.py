"""Integrate the finite renderer startup prefix; no executable allocation."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/renderer-startup';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 m=json.loads((P/'preparation.json').read_text());t=json.loads((P/'test-result.json').read_text());assert m['passed'] and t['passed'] and t['cases']==768 and t['preparation_sha256']==sha(P/'preparation.json') and t['script_sha256']==sha(R/'scripts/test-renderer-startup.py')
 for key,root in [('inputs_sha256',R),('outputs_sha256',P)]:
  for n,h in m[key].items():assert sha(root/n)==h,n
 text=engine.read_text();variant=(P/'engine.ll').read_text();wrappers=['declare i32 @qol_renderer_startup_route_ready()']
 for row in m['records']:
  n=row['name'];body=definition(text,n);assert hashlib.sha256(body.encode()).hexdigest()==row['before_sha256'];text=text.replace(body,body.replace('@'+n+'(','@tribes_before_renderer_startup_'+n+'(',1),1);variant=variant.replace('define hidden fastcc void @'+n+'(','define hidden fastcc void @tribes_renderer_startup_'+n+'(',1)
  wrappers.append(f'''declare hidden fastcc void @tribes_renderer_startup_{n}(%_cpu*)
define hidden fastcc void @{n}(%_cpu* %cpu) {{
 %ready = call i32 @qol_renderer_startup_route_ready()
 %active = icmp ne i32 %ready, 0
 br i1 %active, label %after, label %before
after:
 musttail call fastcc void @tribes_renderer_startup_{n}(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_renderer_startup_{n}(%_cpu* %cpu)
 ret void
}}
''')
 engine.write_text(text+'\n'+'\n'.join(wrappers));path=out/'renderer-startup-engine.ll';path.write_text(variant)
 sites=[('loc_10047335','store i32 %6, i32* %20, align 1',32,'%6','%20'),('loc_10047335','store i8 %33, i8* %32, align 1',8,'%33','%32'),('loc_100473D4','store i32 %2, i32* %9, align 1',32,'%2','%9'),('loc_10047413','store i32 2425393296, i32* %7, align 1',32,'2425393296','%7'),('loc_10047413','store i16 %12, i16* %11, align 1',16,'%12','%11'),('loc_10047436','call void @x86_mov_mem_reg_16(i32 5145085, i32 55689) nounwind',16,'55689',None),('loc_10047492','store i32 %2, i32* %9, align 1',32,'%2','%9')]
 for i,((name,needle,bits,value,pointer),w) in enumerate(zip(sites,m['writes'])):
  body=definition(dll,name);assert body.count(needle)==1
  prefix=f'rs{i}';replacement=''
  if pointer is None:
   replacement+=f'%{prefix}_bp = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 13\n%{prefix}_base = load i64, i64* %{prefix}_bp, align 8\n%{prefix}_address = add i64 %{prefix}_base, {w["address"]}\n%{prefix}_pointer = inttoptr i64 %{prefix}_address to i8*\n'
  else:replacement+=f'%{prefix}_pointer = bitcast i{bits}* {pointer} to i8*\n'
  replacement+=f'%{prefix}_value = zext i{bits} {value} to i64\ncall void @qol_renderer_startup_write(%_cpu* %cpu, i32 {w["pc"]}, i8* %{prefix}_pointer, i32 {w["size"]}, i64 %{prefix}_value)'
  dll=dll.replace(body,body.replace(needle,replacement),1)
 dll+='\ndeclare void @qol_renderer_startup_write(%_cpu*, i32, i8*, i32, i64)\n'
 inc=out/'renderer_startup_publication.inc';inc.write_text('static const TribesCodeWrite renderer_startup_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in m['writes'])+'};\n')
 report=dict(preparation_sha256=sha(P/'preparation.json'),test_sha256=sha(P/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(inc),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'renderer-startup-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
