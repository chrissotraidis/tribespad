"""Bind five exact CALL stores to two finite AOT publication groups."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/renderer-redirects';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 m=json.loads((P/'preparation.json').read_text());t=json.loads((P/'test-result.json').read_text());assert m['passed'] and t['passed'] and t['cases']==1280 and t['preparation_sha256']==sha(P/'preparation.json') and t['script_sha256']==sha(R/'scripts/test-renderer-redirects.py')
 for key,root in [('inputs_sha256',R),('outputs_sha256',P)]:
  for n,h in m[key].items():assert sha(root/n)==h,n
 text=engine.read_text();variant=(P/'engine.ll').read_text();wrappers=['declare i32 @qol_renderer_redirects_route_ready(i32)']
 for row in m['records']:
  n=row['name'];body=definition(text,n);assert hashlib.sha256(body.encode()).hexdigest()==row['before_sha256'];text=text.replace(body,body.replace('@'+n+'(','@tribes_before_renderer_redirects_'+n+'(',1),1);variant=variant.replace('define hidden fastcc void @'+n+'(','define hidden fastcc void @tribes_renderer_redirects_'+n+'(',1)
  wrappers.append(f'''declare hidden fastcc void @tribes_renderer_redirects_{n}(%_cpu*)
define hidden fastcc void @{n}(%_cpu* %cpu) {{
 %ready = call i32 @qol_renderer_redirects_route_ready(i32 {row['group']})
 %active = icmp ne i32 %ready, 0
 br i1 %active, label %after, label %before
after:
 musttail call fastcc void @tribes_renderer_redirects_{n}(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_renderer_redirects_{n}(%_cpu* %cpu)
 ret void
}}
''')
 engine.write_text(text+'\n'+'\n'.join(wrappers));path=out/'renderer-redirects-engine.ll';path.write_text(variant)
 for (name,value,pointer),w in zip([('loc_10032CBA','%3','%8'),('loc_10032D0D','%3','%8'),('loc_10032D60','%2','%9'),('loc_1004750F','%3','%8'),('loc_10047562','%2','%9')],m['writes']):
  body=definition(dll,name);needle=f'store i32 {value}, i32* {pointer}, align 1';assert body.count(needle)==1
  replacement=f'%redirect_pointer = bitcast i32* {pointer} to i8*\n%redirect_value = zext i32 {value} to i64\ncall void @qol_renderer_redirects_write(%_cpu* %cpu, i32 {w["pc"]}, i8* %redirect_pointer, i32 4, i64 %redirect_value)';dll=dll.replace(body,body.replace(needle,replacement),1)
 dll+='\ndeclare void @qol_renderer_redirects_write(%_cpu*, i32, i8*, i32, i64)\n'
 inc=out/'renderer_redirects_publication.inc';inc.write_text('static const TribesCodeWrite renderer_redirects_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in m['writes'])+'};\n')
 report=dict(preparation_sha256=sha(P/'preparation.json'),test_sha256=sha(P/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(inc),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'renderer-redirects-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
