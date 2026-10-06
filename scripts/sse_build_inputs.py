"""Verified native dependencies shared by SSE harnesses and runtime builds."""
import hashlib,json,pathlib,zipfile
R=pathlib.Path(__file__).resolve().parents[1]
def sse_inputs(platform):
    assert platform in ('macos','device','simulator')
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    dep=next(s for s in json.loads((R/'dependencies.lock.json').read_text())['sources'] if s['name']=='softfloat')
    archive=R/'ref/softfloat/SoftFloat-3e.zip';assert sha(archive)==dep['sha256']
    root=R/dep['path'];include=root/'source/include';headers={}
    with zipfile.ZipFile(archive) as z:
        for p in sorted(include.glob('*.h')):
            member=root.name+'/source/include/'+p.name
            assert p.read_bytes()==z.read(member),'modified SoftFloat header: '+p.name
            headers[str(p.relative_to(R))]=sha(p)
    library=R/'generated/softfloat'/platform/'softfloat.a'
    report=json.loads((library.parent/'result.json').read_text())
    assert report['thread_local'] and report['specialization']=='8086'
    assert report['source_sha256']==dep['sha256'] and report['library_sha256']==sha(library)
    adapter=R/'port/core/sse_arithmetic.c'
    return {'compile_flags':['-std=c11','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(include)],
            'sources':[str(adapter)],'libraries':[str(library)],
            'evidence':{'platform':platform,'archive_sha256':dep['sha256'],'library_sha256':sha(library),
                        'headers_sha256':headers,'adapter_sha256':sha(adapter),
                        'adapter_header_sha256':sha(R/'port/core/sse_arithmetic.h'),
                        'build_helper_sha256':sha(pathlib.Path(__file__))}}
