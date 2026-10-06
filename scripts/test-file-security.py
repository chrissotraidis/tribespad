#!/usr/bin/env python3
"""Check existing-file SECURITY_ATTRIBUTES through the real emitted guest ABI."""
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/file-security';out.mkdir(exist_ok=True)
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py')
gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll'
wrapper.write_text(gen.generate({n:json.loads(bindings.read_text())[n] for n in ['CreateFileA','CreateFileW']}))
sources=[R/n for n in ['port/core/guest_file.c','port/core/guest_filesystem.c','port/core/native_handles.c','port/core/guest_heap.c','tests/fixtures/file_security_dispatch.c','tests/fixtures/file_security_calls.c','tests/fixtures/file_security_wrong_abi.ll','tests/fixtures/native_enter.ll']]+[wrapper]
test=R/'tests/file_security_test.c'
guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c']
flags=['clang','-target','arm64-apple-macos12','-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module','-pthread']
runs=[]
for mode in ['normal','tsan','checked']:
    exe=out/mode
    if mode=='checked':
        objects,checked=compile_support(sources,out/'checked-support',[])
        meta=json.loads((out/'checked-support/result.json').read_text());entries=[]
        for obj in objects:
            p=pathlib.Path(obj);row=meta['objects'][p.stem]
            entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
        objects,guarded=guard_link(entries,out/'guarded')
        cmd=flags+['-DCHECKED',str(test),*map(str,guards),*objects,'-o',str(exe)]
    else:
        cmd=flags+(['-fsanitize=thread'] if mode=='tsan' else [])+list(map(str,sources+guards+[test]))+['-o',str(exe)]
    build=subprocess.run(cmd,capture_output=True,text=True)
    (out/(mode+'-build.log')).write_text(build.stdout+build.stderr);assert not build.returncode,build.stderr
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
    (out/(mode+'.log')).write_text(run.stdout+run.stderr)
    assert run.returncode==0 and run.stdout.startswith('PASS:'),(mode,run.returncode,run.stdout,run.stderr)
    if mode!='checked':assert not run.stderr,run.stderr
    else:assert run.stderr.count('checked guest memory violation')==2 and run.stderr.count('external memory contract stop')==3,run.stderr
    runs.append(dict(mode=mode,passed=True,binary_sha256=sha(exe),log_sha256=sha(out/(mode+'.log'))));print(mode,run.stdout.strip())
report=dict(passed=True,runs=runs,checked_build_sha256=checked,guarded_build_sha256=guarded,
            sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+guards+[test,bindings,R/'tests/fixtures/file_metadata_cases.h',R/'port/core/guest_filesystem.h',R/'port/core/native_handles.h',R/'port/core/guest_file.h',R/'port/llasm-support/llasm_cpu.h',R/'port/compiler/ExternalMemoryPass.cpp',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]},
            scope='OPEN_EXISTING A/W accepts guest 12-byte SECURITY_ATTRIBUTES, ignores the descriptor pointer, and sets real descriptor inheritance. Twelve emitted-ABI and exec-inheritance cases per build plus metadata/delete rights and sharing, backup directories, final/dangling/escaping symlink objects, missing-file/invalid layout/overflow checks, two protected-read stops and three realpath guest-pointer/wrong-ABI stops. Host component only; Windows child-process creation and new-file security remain unimplemented.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
