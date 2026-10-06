"""Bind the verified two-store clock revision to its original writer sites."""
import hashlib,json,pathlib
from clock_patch_inputs import R,OUT,sha,definition
def prepare(dll,engine,out):
    meta=json.loads((OUT/'preparation.json').read_text());test=json.loads((OUT/'test-result.json').read_text())
    assert meta['passed'] and not meta['published'] and test['passed'] and not test['unvisited_targets']
    assert test['preparation_sha256']==sha(OUT/'preparation.json') and test['definitions']==7 and test['cases']==1792 and test['whole_path_cases']==512
    for p,h in meta['inputs_sha256'].items():assert sha(R/p)==h
    for p,h in meta['outputs_sha256'].items():assert sha(OUT/p)==h
    for p,h in test['sources_sha256'].items():assert sha(R/p)==h
    for p,h in test['output_sha256'].items():assert sha(OUT/p)==h,p
    for n,h in dict(meta['target_definitions'],**meta['helper_definitions']).items():assert hashlib.sha256(definition(dll,n).encode()).hexdigest()==h
    original=engine.read_text();variant=(OUT/'engine.ll').read_text();wrappers=['declare i32 @qol_clock_patch_route_ready()']
    for row in meta['records']:
        name=row['name'];body=definition(original,name)
        assert hashlib.sha256(body.encode()).hexdigest()==row['old_definition_sha256']
        original=original.replace(body,body.replace('@'+name+'(','@tribes_before_clock_'+name+'(',1),1)
        variant=variant.replace('define hidden fastcc void @'+name+'(','define hidden fastcc void @tribes_clock_'+name+'(',1)
        wrappers.append('''declare hidden fastcc void @tribes_clock_%s(%%_cpu*)
define hidden fastcc void @%s(%%_cpu* %%cpu) {
 %%ready = call i32 @qol_clock_patch_route_ready()
 %%patched = icmp ne i32 %%ready, 0
 br i1 %%patched, label %%after, label %%before
after:
 musttail call fastcc void @tribes_clock_%s(%%_cpu* %%cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_clock_%s(%%_cpu* %%cpu)
 ret void
}
'''%(name,name,name,name))
    engine.write_text(original+'\n'+'\n'.join(wrappers));variant_path=out/'clock-patch-engine.ll';variant_path.write_text(variant)
    writers=[]
    for name,pc,pointer,value in [('loc_1006080A',0x1006080f,8,3),('loc_1006086C',0x1006086c,9,2)]:
        body=definition(dll,name);needle='store i32 %%%d, i32* %%%d, align 1\n'%(value,pointer);assert body.count(needle)==1
        replacement='%%clock_p = bitcast i32* %%%d to i8*\n%%clock_v = zext i32 %%%d to i64\ncall void @qol_clock_patch_write(%%_cpu* %%cpu, i32 %d, i8* %%clock_p, i32 4, i64 %%clock_v)\n'%(pointer,value,pc)
        dll=dll.replace(body,body.replace(needle,replacement),1);writers.append(dict(name=name,pc=pc,original_sha256=hashlib.sha256(body.encode()).hexdigest()))
    dll+='\ndeclare void @qol_clock_patch_write(%_cpu*, i32, i8*, i32, i64)\ndeclare void @qol_clock_patch_checkpoint(%_cpu*)\n'
    needle='define hidden fastcc void @loc_1006086E(%_cpu* %cpu) nounwind {\n';assert dll.count(needle)==1
    dll=dll.replace(needle,needle+'call void @qol_clock_patch_checkpoint(%_cpu* %cpu)\n')
    records=[]
    for r in meta['records']:
        w=r['write'];records.append('{%du,%du,%du,UINT64_C(%d),UINT64_C(%d)}'%(w['pc'],w['address'],w['size'],int.from_bytes(bytes.fromhex(w['before']),'little'),int.from_bytes(bytes.fromhex(w['after']),'little')))
    include=out/'clock_patch_publication.inc';include.write_text('static const TribesCodeWrite clock_patch_writes[]={'+','.join(records)+'};\n')
    report=dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),variant_sha256=sha(variant_path),engine_sha256=sha(engine),include_sha256=sha(include),writers=writers,script_sha256=sha(pathlib.Path(__file__).resolve()),
        scope='Two finite displacement stores; activate both verified CALL blocks immediately after the second original store. The only intervening import is VirtualProtect. Earlier routes retained after completion; partial routes and all ordinary published-code writes remain blocked.')
    (out/'clock-patch-inputs.json').write_text(json.dumps(report,indent=2)+'\n');return dll,variant_path,report
