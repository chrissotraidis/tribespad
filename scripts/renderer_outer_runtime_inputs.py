"""Bind the pre-memset renderer suffix to its finite compiled revision."""
import hashlib,json,pathlib
import capstone,pefile
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/renderer-outer'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def definition(text,name):
 begin=text.find('define hidden fastcc void @'+name+'(')
 if begin<0:begin=text.find('define protected fastcc void @'+name+'(')
 assert begin>=0,name
 return text[begin:text.index('\n}',begin)+3]
def prepare(dll,engine,out):
 m=json.loads((P/'preparation.json').read_text());t=json.loads((P/'test-result.json').read_text())
 assert m['passed'] and t['passed'] and t['cases']==2304 and t['preparation_sha256']==sha(P/'preparation.json') and t['script_sha256']==sha(R/'scripts/test-renderer-outer.py')
 for key,root in [('inputs_sha256',R),('outputs_sha256',P)]:
  for n,h in m[key].items():assert sha(root/n)==h,n
 text=engine.read_text();variant=(P/'engine.ll').read_text();wrappers=['declare i32 @qol_renderer_outer_route_ready()']
 for row in m['records']:
  n=row['name'];body=definition(text,n);assert hashlib.sha256(body.encode()).hexdigest()==row['before_sha256'],n
  assert text.count(body)==1,n
  text=text.replace(body,body.replace('@'+n+'(','@tribes_before_renderer_outer_'+n+'(',1),1)
  variant=variant.replace('define hidden fastcc void @'+n+'(','define hidden fastcc void @tribes_renderer_outer_'+n+'(',1)
  wrappers.append(f'''declare hidden fastcc void @tribes_renderer_outer_{n}(%_cpu*)
define hidden fastcc void @{n}(%_cpu* %cpu) {{
 %ready = call i32 @qol_renderer_outer_route_ready()
 %active = icmp ne i32 %ready, 0
 br i1 %active, label %after, label %before
after:
 musttail call fastcc void @tribes_renderer_outer_{n}(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_renderer_outer_{n}(%_cpu* %cpu)
 ret void
}}
''')
 engine.write_text(text+'\n'+'\n'.join(wrappers));path=out/'renderer-outer-engine.ll';path.write_text(variant)
 originals={w['writer_name']:definition(dll,w['writer_name']) for w in m['writes'] if not w.get('shared')}
 for i,w in enumerate(m['writes']):
  if w.get('shared'):continue
  name=w['writer_name'];assert hashlib.sha256(originals[name].encode()).hexdigest()==w['writer_sha256']
  body=definition(dll,name);needle=w['store'];assert body.count(needle)==1
  replacement=f'%batch_pointer{i} = bitcast i{w["bits"]}* {w["pointer_operand"]} to i8*\n%batch_value{i} = zext i{w["bits"]} {w["value_operand"]} to i64\ncall void @qol_renderer_outer_write(%_cpu* %cpu, i32 {w["pc"]}, i8* %batch_pointer{i}, i32 {w["size"]}, i64 %batch_value{i})'
  assert dll.count(body)==1,name
  dll=dll.replace(body,body.replace(needle,replacement),1)
 dll+='\ndeclare void @qol_renderer_outer_write(%_cpu*, i32, i8*, i32, i64)\n'
 for pc,name in {(w['pc'],w['writer_name']) for w in m['writes'] if w.get('shared')}:
  body=definition(dll,name)
  assert f'call void @qol_string_shared_write(%_cpu* %cpu, i32 {pc},' in body
 inc=out/'renderer_outer_publication.inc';inc.write_text('static const TribesCodeWrite renderer_outer_writes[]={'+','.join('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],w['before'],w['value']) for w in m['writes'])+'};\n')
 report=dict(preparation_sha256=sha(P/'preparation.json'),test_sha256=sha(P/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(inc),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'renderer-outer-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
