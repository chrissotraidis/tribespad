#!/usr/bin/env python3
import hashlib,importlib.util,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/error-mode';out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';all_bindings=json.loads(bindings.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({name:all_bindings[name] for name in ['SetErrorMode','GetErrorMode']}))
sources=[R/name for name in ['port/core/exception_filter.c','port/core/guest_heap.c','tests/error_mode_abi_test.c','tests/fixtures/native_enter.ll']]+[wrapper];runs=[]
for mode in ['normal','tsan']:
 exe=out/mode;subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wall','-Wextra','-Werror','-Wno-override-module','-DPTROFS_64BIT']+(['-fsanitize=thread'] if mode=='tsan' else [])+list(map(str,sources))+['-o',str(exe)],check=True)
 run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0 and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr);runs.append(dict(mode=mode,binary_sha256=sha(exe),output=run.stdout.strip()))
report=dict(passed=True,runs=runs,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[bindings,R/'port/llasm-support/llasm_cpu.h',pathlib.Path(__file__).resolve()]},scope='One guest process per native runtime: actual emitted Set/GetErrorMode state/ABI and concurrency. Native services have no Windows error-dialog UI. No Windows fault/SEH delivery or child-process inheritance acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
