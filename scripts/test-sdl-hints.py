#!/usr/bin/env python3
"""Execute real native SDL hints through guest ABI and both memory guards."""
import argparse,hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
from sdl_backend_inputs import inputs
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('--simulator-device');a=ap.parse_args()
if a.simulator_device:
    d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [v['udid'] for rows in d['devices'].values() for v in rows if v['state']=='Booted']==[a.simulator_device]
platform='simulator' if a.simulator_device else 'macos'
out=R/'generated/integration-review/sdl-hints';out=out/a.simulator_device if a.simulator_device else out;out.mkdir(parents=True,exist_ok=True)
(out/'result.json').unlink(missing_ok=True)
backend,flags,libraries=inputs(platform)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=json.loads((R/'config/runtime/sdl-bindings.json').read_text())
for name,helper,n,returns in [('TestMalloc','sdl_test_malloc',1,True),('TestCalloc','sdl_test_calloc',2,True),('TestRealloc','sdl_test_realloc',2,True),('TestFree','sdl_test_free',1,False),('TestLog','sdl_test_log',4,False)]:
    bindings[name]=dict(helper=helper,arguments=n,returns=returns,guest_base=False,cpu_argument=True,calling_convention='cdecl')
wrapper=out/'bindings.ll';wrapper.write_text(gen.generate(bindings,False))
sources=[R/n for n in ['port/core/guest_sdl_backend.c','port/core/guest_window_catalog.c','port/core/guest_sdl_sync.c','port/core/guest_sdl_tls.c','port/core/guest_sdl_memory.c','port/core/guest_curl_allocator.c','port/core/guest_callback.c','port/core/guest_heap.c','port/core/process_environment.c','tests/fixtures/sdl_hint_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
fixture=out/'contracts.ll';fixture.write_text('''declare i1 @SDL_SetAppMetadata(ptr,ptr,ptr)
define void @sdl_test_metadata_guest_pointer(ptr %p) { %r=call i1 @SDL_SetAppMetadata(ptr %p,ptr null,ptr null)
 ret void
}
declare i1 @SDL_SetHintWithPriority(ptr,ptr,i32)
declare i1 @SDL_SetMemoryFunctions(ptr,ptr,ptr,ptr)
define void @sdl_test_guest_pointer(ptr %p) { %r=call i1 @SDL_SetHintWithPriority(ptr %p,ptr null,i32 1)
 ret void
}
define void @sdl_test_bad_callback(ptr %p) { %r=call i1 @SDL_SetMemoryFunctions(ptr %p,ptr %p,ptr %p,ptr %p)
 ret void
}
''')
wrong=out/'wrong.ll';wrong.write_text('''declare i32 @SDL_SetAppMetadata(ptr,ptr,ptr)
define void @sdl_test_metadata_wrong_abi() { %r=call i32 @SDL_SetAppMetadata(ptr null,ptr null,ptr null)
 ret void
}
declare i32 @SDL_SetHintWithPriority(ptr,ptr,i32)
define void @sdl_test_wrong_abi() { %r=call i32 @SDL_SetHintWithPriority(ptr null,ptr null,i32 1)
 ret void
}
''')
fixture.write_text(fixture.read_text()+"""
declare void @SDL_SetLogOutputFunction(ptr,ptr)
declare void @SDL_LogMessage(i32,i32,ptr,...)
@log_format = private constant [3 x i8] c"%s\\00"
@bad_log_format = private constant [3 x i8] c"%n\\00"
define void @sdl_test_log_bad_callback(ptr %p) { call void @SDL_SetLogOutputFunction(ptr %p,ptr null)
 ret void
}
define void @sdl_test_log_guest_userdata(ptr %p) { call void @SDL_SetLogOutputFunction(ptr null,ptr %p)
 ret void
}
define void @sdl_test_log_guest_message(ptr %p) { call void (i32,i32,ptr,...) @SDL_LogMessage(i32 0,i32 5,ptr @log_format,ptr %p)
 ret void
}
define void @sdl_test_log_bad_format() { call void (i32,i32,ptr,...) @SDL_LogMessage(i32 0,i32 5,ptr @bad_log_format,ptr null)
 ret void
}
""")
wrong.write_text(wrong.read_text()+"""
declare i32 @SDL_SetLogOutputFunction(ptr,ptr)
define void @sdl_test_log_wrong_abi() { %r=call i32 @SDL_SetLogOutputFunction(ptr null,ptr null)
 ret void
}
declare void @SDL_LogMessage(i32,...)
@wrong_log_format = private constant [3 x i8] c"%s\\00"
define void @sdl_test_log_wrong_varargs() { call void (i32,...) @SDL_LogMessage(i32 0,i32 5,ptr @wrong_log_format,ptr null)
 ret void
}
""")
wrong.write_text(wrong.read_text()+"""
declare i32 @SDL_Init(i32)
define void @sdl_test_init_wrong_abi() { %r=call i32 @SDL_Init(i32 0)
 ret void
}
declare i64 @SDL_WasInit(i32)
define void @sdl_test_was_init_wrong_abi() { %r=call i64 @SDL_WasInit(i32 0)
 ret void
}
declare i32 @SDL_QuitSubSystem(i32)
define void @sdl_test_quit_subsystem_wrong_abi() { %r=call i32 @SDL_QuitSubSystem(i32 0)
 ret void
}
""")
objects=[];checked=guarded=None
objects,checked=compile_support(sources+[fixture,wrong],out/'checked',['-DPTROFS_64BIT','-DTRIBESPAD_SDL_TEST_OBSERVER',*flags],target=('arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'),sdk_name=('iphonesimulator' if a.simulator_device else 'macosx'))
meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
    p=pathlib.Path(obj);entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=meta['objects'][p.stem]['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded',target=('arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'))
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];test=R/'tests/sdl_hint_test.c'
report=dict(passed=False,backend_sha256=sha(R/'generated/sdl-backend'/platform/'result.json'),checked_sha256=checked,guarded_sha256=guarded)
for mode in (['checked'] if a.simulator_device else ['normal','tsan','checked']):
    exe=out/('test-'+mode)
    command=['clang','-target',('arm64-apple-ios16-simulator' if a.simulator_device else 'arm64-apple-macos12'),'-isysroot',backend['sdk'],'-O1','-g','-DPTROFS_64BIT','-DTRIBESPAD_SDL_TEST_OBSERVER','-Wno-override-module','-Wall','-Wextra','-Werror',*flags,str(test),*map(str,guards)]
    command+=[('-DCHECKED_GUARDS' if a.simulator_device else '-DCHECKED_TEST'),*objects] if mode=='checked' else list(map(str,sources))
    if mode=='tsan':command+=['-fsanitize=thread']
    command+=[*libraries,'-o',str(exe)];subprocess.run(command,check=True)
    run=subprocess.run(['xcrun','simctl','spawn',a.simulator_device,str(exe)] if a.simulator_device else [str(exe)],capture_output=True,text=True,timeout=60)
    (out/(mode+'.log')).write_text(run.stdout+run.stderr)
    assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
    if mode=='checked' and not a.simulator_device:
        assert 'checked guest memory violation' in run.stderr
        assert 'unverified call: SDL_SetHintWithPriority' in run.stderr
        assert 'unverified call: SDL_SetAppMetadata' in run.stderr
        assert 'unverified call: SDL_SetLogOutputFunction' in run.stderr
        assert 'unverified call: SDL_LogMessage' in run.stderr
        assert 'unverified call: SDL_Init' in run.stderr
        assert 'unverified call: SDL_WasInit' in run.stderr
        assert 'unverified call: SDL_QuitSubSystem' in run.stderr
        assert 'unsupported SDL subsystem initialization flags' in run.stderr
        assert run.stderr.count('unsupported SDL backend ownership/lifecycle')==3
    else:assert not run.stderr,run.stderr
    report[mode]=dict(binary_sha256=sha(exe),stdout=run.stdout);print(mode,run.stdout.strip())
paths=sources+guards+[fixture,wrong,test,R/'port/core/guest_sdl_backend.h',R/'config/runtime/sdl-bindings.json',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',R/'scripts/sdl_backend_inputs.py',pathlib.Path(__file__).resolve()]
report.update(passed=True,simulator_device=a.simulator_device,sources_sha256={str(p.relative_to(R)):sha(p) for p in paths},scope='Native SDL event lifecycle/logging/hints/metadata and allocator through actual guest ABI. Host normal/TSan/guarded modes or one selected simulator; host-only fault subprocesses. No native window, event loop, renderer or gameplay acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
