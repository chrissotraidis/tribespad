"""Verified native SDL library and explicit guest ABI bindings."""
import hashlib
import importlib.util
import json
import pathlib
import shlex
R=pathlib.Path(__file__).resolve().parents[1]
def inputs(platform='macos'):
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    root=R/'generated/sdl-backend'/platform
    report=json.loads((root/'result.json').read_text())
    assert report['passed'] and report['platform']==platform
    assert report['config_sha256']==sha(R/'config/runtime/sdl-backend.json')
    libraries=[]
    for name,digest in report['libraries'].items():
        assert sha(R/name)==digest,name
        libraries.append(str(R/name))
    for name,digest in report['headers'].items():assert sha(root/'install'/name)==digest,name
    line=next(x for x in report['pkgconfig'].splitlines() if x.startswith('Libs:'))
    libraries += [x for x in shlex.split(line[5:]) if not x.startswith('-L') and x!='-lSDL3']
    return report,['-I',str(root/'install/include'),'-Wno-deprecated-declarations'],libraries+['-framework','OpenGL' if platform=='macos' else 'OpenGLES']
def prepare(destination):
    report,flags,libraries=inputs()
    bindings=json.loads((R/'config/runtime/sdl-bindings.json').read_text())
    spec=importlib.util.spec_from_file_location('sdl_binding_generator',R/'scripts/gen-win32-bindings.py')
    gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
    wrapper=destination/'sdl-bindings.ll';wrapper.write_text(gen.generate(bindings,False))
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    metadata=dict(backend_report_sha256=sha(R/'generated/sdl-backend/macos/result.json'),
                  libraries=report['libraries'],bindings_sha256=sha(R/'config/runtime/sdl-bindings.json'),
                  script_sha256=sha(pathlib.Path(__file__)),scope='Native private SDL storage, checked guest public results, Apple audio initialization, typed synchronization, TLS/cdecl cleanup, keyboard/controller startup, and typed native windows/GL contexts. Finite GL/GLU queries, initial native clear/framebuffer drawing and window/DC adaptation are included; general rendering, mobile draw-buffer selection, event conversion and audio streams remain unfinished.')
    from graphics_backend_inputs import prepare as prepare_graphics
    graphics_sources,graphics_bindings=prepare_graphics(destination)
    bindings.update(graphics_bindings)
    from dsound_backend_inputs import prepare as prepare_dsound
    dsound_sources,dsound_bindings=prepare_dsound(destination)
    bindings.update(dsound_bindings)
    metadata['dsound_inputs_sha256']={str(p.relative_to(R)):sha(p) for p in [R/'scripts/dsound_backend_inputs.py',R/'port/core/guest_dsound.c',R/'port/core/guest_dsound.h']}
    metadata['dsound_exports_sha256']=sha(destination/'dsound-exports.inc')
    metadata['graphics_exports_sha256']=sha(destination/'graphics-exports.inc')
    metadata['graphics_inputs_sha256']={str(p.relative_to(R)):sha(p) for p in [R/'scripts/graphics_backend_inputs.py',R/'port/core/guest_graphics.h',R/'port/core/guest_graphics.c']}
    metadata['graphics_bindings']=graphics_bindings
    from openal_backend_inputs import prepare as prepare_openal,inputs as openal_inputs
    openal_sources,openal_bindings=prepare_openal(destination)
    openal_report,openal_flags,openal_libraries=openal_inputs()
    flags+=openal_flags;libraries+=openal_libraries;bindings.update(openal_bindings)
    metadata['openal_backend']=openal_report
    metadata['openal_inputs_sha256']={str(p.relative_to(R)):sha(p) for p in [R/'scripts/openal_backend_inputs.py',R/'port/core/guest_openal.c',R/'port/core/guest_openal.h']}
    return graphics_sources+dsound_sources+openal_sources+[R/'port/core/guest_sdl_backend.c',R/'port/core/guest_sdl_sync.c',R/'port/core/guest_sdl_tls.c',wrapper],flags,libraries,bindings,metadata
