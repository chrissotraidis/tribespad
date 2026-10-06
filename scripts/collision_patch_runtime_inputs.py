"""Bind the verified collision-query CALL to its one exact publication store."""
import hashlib,json,pathlib
from collision_patch_inputs import R,OUT,sha,definition
def prepare(dll,engine,out):
    meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text())
    assert meta['passed'] and not meta['published'] and test['passed'] and not test['unvisited_targets']
    assert test['preparation_sha256']==sha(OUT/'preparation.json') and test['definitions']==7 and test['cases']==896
    for p,h in meta['inputs_sha256'].items():assert sha(R/p)==h,p
    for p,h in meta['outputs_sha256'].items():assert sha(OUT/p)==h,p
    for p,h in test['sources_sha256'].items():assert sha(R/p)==h,p
    for p,h in test['output_sha256'].items():assert sha(OUT/p)==h,p
    from sse_build_inputs import sse_inputs
    assert test['math']==sse_inputs('macos')['evidence']
    for n,h in meta['target_definitions'].items():assert hashlib.sha256(definition(dll,n).encode()).hexdigest()==h,n
    row=meta['records'][0];name=row['name'];original=engine.read_text();body=definition(original,name)
    assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256']
    original=original.replace(body,body.replace('@'+name+'(','@tribes_before_collision_'+name+'(',1),1)
    variant=(OUT/'engine.ll').read_text().replace('define hidden fastcc void @'+name+'(','define hidden fastcc void @tribes_collision_'+name+'(',1)
    wrapper='''declare i32 @qol_collision_patch_route_ready()
declare hidden fastcc void @tribes_collision_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_collision_patch_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_collision_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_collision_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name)
    engine.write_text(original+'\n'+wrapper);variant_path=out/'collision-patch-engine.ll';variant_path.write_text(variant)
    writer=definition(dll,'loc_1000C3A5');needle='store i32 %2, i32* %9, align 1\n';assert writer.count(needle)==1
    replacement='''%collision_p = bitcast i32* %9 to i8*
%collision_v = zext i32 %2 to i64
call void @qol_collision_patch_write(%_cpu* %cpu, i32 268485541, i8* %collision_p, i32 4, i64 %collision_v)
'''
    dll=dll.replace(writer,writer.replace(needle,replacement),1)
    dll+='\ndeclare void @qol_collision_patch_write(%_cpu*, i32, i8*, i32, i64)\ndeclare void @qol_collision_patch_checkpoint(%_cpu*)\n'
    marker='define hidden fastcc void @loc_1000C3A7(%_cpu* %cpu) nounwind {\n';assert dll.count(marker)==1
    dll=dll.replace(marker,marker+'call void @qol_collision_patch_checkpoint(%_cpu* %cpu)\n')
    w=row['write'];include=out/'collision_patch_publication.inc'
    include.write_text('static const TribesCodeWrite collision_patch_code_write={%du,%du,%du,UINT64_C(%d),UINT64_C(%d)};\n'%(w['pc'],w['address'],w['size'],int.from_bytes(bytes.fromhex(w['before']),'little'),int.from_bytes(bytes.fromhex(w['after']),'little')))
    report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(variant_path),engine_sha256=sha(engine),include_sha256=sha(include),writer_sha256=hashlib.sha256(writer.encode()).hexdigest(),script_sha256=sha(pathlib.Path(__file__).resolve()),
                scope='One exact original displacement store activates one verified CALL block immediately. The six target blocks retain their original bytes/lowering. Earlier routes remain active; unknown mutations and partial dispatch remain forbidden. No pending hook entry is published.')
    (out/'collision-patch-inputs.json').write_text(json.dumps(report,indent=2)+'\n')
    return dll,variant_path,report
