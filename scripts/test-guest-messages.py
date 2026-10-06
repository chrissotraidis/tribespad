#!/usr/bin/env python3
import hashlib,importlib.util,json,pathlib,subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/guest-messages';out.mkdir(exist_ok=True)
subprocess.run(['python3',str(R/'scripts/gen-system-messages.py'),'--check'],check=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';all_bindings=json.loads(bindings.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({k:all_bindings[k] for k in ['FormatMessageA','FormatMessageW','LocalAlloc','LocalFree','LocalSize']}))
sources=[R/'port/core/guest_messages.c',R/'port/core/guest_heap.c',R/'tests/fixtures/native_enter.ll',wrapper];objects,build=compile_support(sources,out/'checked',[]);exe=out/'test';test=R/'tests/guest_messages_test.c';guard=R/'port/core/guest_memory_checks.c'
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wall','-Wextra','-Werror','-DPTROFS_64BIT',str(test),str(guard),*objects,'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr);assert not run.returncode and run.stderr.count('checked guest memory violation')==4,(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,checked_support_sha256=build,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[test,guard,bindings,R/'port/llasm-support/llasm_cpu.h',R/'port/core/guest_messages.h',R/'port/core/system_messages.inc',R/'config/runtime/system-messages.json',R/'scripts/gen-system-messages.py',pathlib.Path(__file__).resolve()]},scope='Finite source-bound English catalog, actual guest memory allocation/free, ANSI/UTF16 and checked output faults. Other resources/templates/inserts/languages and unlisted catalog IDs are not implemented.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
