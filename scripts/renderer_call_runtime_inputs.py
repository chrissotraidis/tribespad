"""Integrate the one observed renderer CALL store and its finite route."""
import hashlib,json,pathlib
from console_fade_inputs import definition
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/renderer-call';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def prepare(dll,engine,out):
 meta=json.loads((P/'preparation.json').read_text());test=json.loads((P/'test-result.json').read_text());assert meta['passed'] and test['passed'] and test['cases']==256 and test['preparation_sha256']==sha(P/'preparation.json') and test['script_sha256']==sha(R/'scripts/test-renderer-call.py')
 for k,root in [('inputs_sha256',R),('outputs_sha256',P)]:
  for n,h in meta[k].items():assert sha(root/n)==h,n
 text=engine.read_text();name=meta['entry'];body=definition(text,name);assert hashlib.sha256(body.encode()).hexdigest()==meta['before_sha256']
 variant=(P/'engine.ll').read_text().replace('@'+name+'(','@tribes_renderer_'+name+'(')
 text=text.replace(body,body.replace('@'+name+'(','@tribes_before_renderer_'+name+'(',1),1)
 text+='''\ndeclare i32 @qol_renderer_call_route_ready()
declare hidden fastcc void @tribes_renderer_loc_505D13(%_cpu*)
define hidden fastcc void @loc_505D13(%_cpu* %cpu) {
 %ready = call i32 @qol_renderer_call_route_ready()
 %active = icmp ne i32 %ready, 0
 br i1 %active, label %after, label %before
after:
 musttail call fastcc void @tribes_renderer_loc_505D13(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_renderer_loc_505D13(%_cpu* %cpu)
 ret void
}
''';engine.write_text(text);path=out/'renderer-call-engine.ll';path.write_text(variant)
 body=definition(dll,'loc_10047071');needle='store i32 %2, i32* %9, align 1'
 assert body.count(needle)==1,body
 changed=body.replace(needle,'''%renderer_pointer = bitcast i32* %9 to i8*
%renderer_value = zext i32 %2 to i64
call void @qol_renderer_call_write(%_cpu* %cpu, i32 268726385, i8* %renderer_pointer, i32 4, i64 %renderer_value)''')
 dll=dll.replace(body,changed,1)+'\ndeclare void @qol_renderer_call_write(%_cpu*, i32, i8*, i32, i64)\n'
 w=meta['write'];inc=out/'renderer_call_publication.inc';inc.write_text('static const TribesCodeWrite renderer_call_write={%du,%du,%du,UINT64_C(%d),UINT64_C(%d)};\n'%(w['pc'],w['address'],w['size'],w['before'],w['value']))
 report=dict(preparation_sha256=sha(P/'preparation.json'),test_sha256=sha(P/'test-result.json'),variant_sha256=sha(path),include_sha256=sha(inc),functions=[name],writer_before_sha256=hashlib.sha256(body.encode()).hexdigest(),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (out/'renderer-call-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,path,report
