#!/usr/bin/env python3
"""Compare checked native NTSTATUS conversion with compiled upstream Wine code."""
import hashlib, importlib.util, json, pathlib, re, subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
out=R/'generated/integration-review/ntstatus';out.mkdir(exist_ok=True)
subprocess.run(['python3',str(R/'scripts/gen-ntstatus-table.py'),'--check'],check=True)
upstream=R/'ref/downloads/wine-10.0-ntdll'
# Remove only the SAL annotation include. The error constants and upstream
# table/search/normalization are compiled directly, independently of generator.
winerror=(upstream/'winerror.h').read_text();assert winerror.count('#include <specstrings.h>')==1
(out/'winerror.h').write_text(winerror.replace('#include <specstrings.h>',''))
original=(upstream/'error.c').read_text();start=original.index('ULONG WINAPI RtlNtStatusToDosErrorNoTeb');end=original.index('\n}',start)+2
function=original[start:end].replace('ULONG WINAPI RtlNtStatusToDosErrorNoTeb( NTSTATUS status )','uint32_t reference_ntstatus(uint32_t status)')
function=re.sub(r'    if \(ret == ERROR_MR_MID_NOT_FOUND.*?WARN\(.*?;', '', function, flags=re.S)
source='#include <stdint.h>\n#include <stddef.h>\ntypedef uint32_t DWORD, ULONG, NTSTATUS;\n#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))\n#define HIWORD(x) ((x)>>16)\n#define LOWORD(x) ((x)&65535)\n'
source+='#include "'+str(upstream/'ntstatus.h')+'"\n#include "winerror.h"\n#include "'+str(upstream/'error.h')+'"\n'+function+'\n'
# The statuses are read from the original range endpoints, not generated data.
ranges=[(int(a,16),int(b,16)) for a,b in re.findall(r'\{ (0x[\da-f]+), (0x[\da-f]+), \d+ \}',(upstream/'error.h').read_text())]
statuses=[v for a,b in ranges for v in range(a,b+1)]
source+='const uint32_t reference_statuses[]={'+','.join(hex(v)+'u' for v in statuses)+'};\nconst unsigned reference_count='+str(len(statuses))+';\n'
reference=out/'reference.c';reference.write_text(source)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)
bindings=R/'port/llasm-runtime/win32-bindings.json';profile=json.loads(bindings.read_text());wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({'RtlNtStatusToDosError':profile['RtlNtStatusToDosError']},False))
sources=[R/'port/core/guest_ntstatus.c',R/'port/core/guest_teb.c',R/'port/core/guest_heap.c',wrapper,R/'tests/fixtures/native_enter.ll']
objects,build=compile_support(sources,out/'checked',[])
test=R/'tests/ntstatus_test.c';guard=R/'port/core/guest_memory_checks.c';exe=out/'test'
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wall','-Wextra','-Werror','-DPTROFS_64BIT',str(test),str(guard),str(reference),*objects,'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr)
assert run.returncode==0 and run.stderr.count('checked guest memory violation')==1 and run.stderr.count('sparse TEB')==2,(run.returncode,run.stdout,run.stderr)
files=sources+[test,guard,reference,bindings,R/'port/core/ntstatus_table.inc',R/'port/core/guest_ntstatus.h',R/'port/core/guest_teb.h',R/'config/runtime/ntstatus-sources.json',R/'scripts/gen-ntstatus-table.py',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,output=run.stdout.strip(),checked_support_sha256=build,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in files},scope='Pinned Wine conversion and original TEB status semantics; other NTDLL services remain separate.'),indent=2)+'\n')
print(run.stdout,end='')
