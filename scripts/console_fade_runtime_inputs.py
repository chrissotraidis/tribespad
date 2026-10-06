"""Publish the verified first console CALL redirect at its original store."""
import hashlib, json, pathlib, re
from console_fade_inputs import R, FROZEN, OUT, sha, definition

def prepare(dll, engine, out):
    meta = json.loads((OUT/'preparation.json').read_text())
    test = json.loads((OUT/'test-result.json').read_text())
    assert meta['passed'] and not meta['published'] and test['passed']
    assert test['preparation_sha256'] == sha(OUT/'preparation.json')
    for path,h in meta['inputs_sha256'].items(): assert sha(R/path) == h
    for path,h in meta['outputs_sha256'].items(): assert sha(OUT/path) == h
    for path,h in test['sources_sha256'].items(): assert sha(R/path) == h
    from sse_build_inputs import sse_inputs
    assert sse_inputs('macos')['evidence'] == test['math']
    assert test['call_block_cases'] == 256 and test['whole_fade_cases'] == 3392
    # The selected native target is the same complete, tested original routine.
    for name,h in meta['fade_definitions'].items():
        assert hashlib.sha256(definition(dll,name).encode()).hexdigest() == h, name
    original = engine.read_text()
    body = definition(original,'loc_5D2842')
    assert hashlib.sha256(body.encode()).hexdigest() == meta['old_definition_sha256']
    original = original.replace(body,body.replace('@loc_5D2842(', '@tribes_before_console_fade_loc_5D2842(',1),1)
    wrapper = '''
declare i32 @qol_console_fade_route_ready()
declare hidden fastcc void @tribes_console_fade_loc_5D2842(%_cpu*)
define hidden fastcc void @loc_5D2842(%_cpu* %cpu) {
 %ready = call i32 @qol_console_fade_route_ready()
 %patched = icmp ne i32 %ready, 0
 br i1 %patched, label %after, label %before
after:
 musttail call fastcc void @tribes_console_fade_loc_5D2842(%_cpu* %cpu)
 ret void
before:
 musttail call fastcc void @tribes_before_console_fade_loc_5D2842(%_cpu* %cpu)
 ret void
}
'''
    engine.write_text(original + wrapper)
    variant = (OUT/'engine.ll').read_text().replace('define hidden fastcc void @loc_5D2842(', 'define hidden fastcc void @tribes_console_fade_loc_5D2842(',1)
    variant_path = out/'console-fade-engine.ll'; variant_path.write_text(variant)
    writer = definition(dll,'loc_10002931')
    needle = 'store i32 %3, i32* %8, align 1\n'
    assert writer.count(needle) == 1
    replacement = '''%console_fade_p = bitcast i32* %8 to i8*
%console_fade_v = zext i32 %3 to i64
call void @qol_console_fade_write(%_cpu* %cpu, i32 268446006, i8* %console_fade_p, i32 4, i64 %console_fade_v)
'''
    dll = dll.replace(writer,writer.replace(needle,replacement),1)
    assert 268446006 == 0x10002936
    dll += '\ndeclare void @qol_console_fade_write(%_cpu*, i32, i8*, i32, i64)\ndeclare void @qol_console_fade_checkpoint(%_cpu*)\n'
    needle = 'define hidden fastcc void @loc_10002938(%_cpu* %cpu) nounwind {\n'
    assert dll.count(needle) == 1
    dll = dll.replace(needle,needle+'call void @qol_console_fade_checkpoint(%_cpu* %cpu)\n')
    w = meta['write']
    include = out/'console_fade_publication.inc'
    include.write_text('static const TribesCodeWrite console_fade_code_write={%du,%du,%du,UINT64_C(%d),UINT64_C(%d)};\n'%(w['pc'],w['address'],w['size'],w['before'],w['value']))
    report = dict(preparation_sha256=sha(OUT/'preparation.json'),test_sha256=sha(OUT/'test-result.json'),
        variant_sha256=sha(variant_path),engine_sha256=sha(engine),include_sha256=sha(include),
        writer_original_sha256=hashlib.sha256(writer.encode()).hexdigest(),script_sha256=sha(pathlib.Path(__file__).resolve()),
        scope='First console CALL displacement only: original store performs one finite, source-bound write and immediately selects the tested block. Later console writes remain forbidden.')
    (out/'console-fade-inputs.json').write_text(json.dumps(report,indent=2)+'\n')
    return dll,variant_path,report
