#!/usr/bin/env python3
"""Execute the exact DLL self-entry switch before and after publication."""
import ctypes,hashlib,json,pathlib,random,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=R/'generated/srw/run-20261002T170057-ifc22/native-diagnostic/checked/bootstrap-published/qol-patched.ll'
metadata=json.loads(source.with_name('patch-inputs.json').read_text());assert sha(source)==metadata['sources_sha256'][str(source.relative_to(R))]
text=source.read_text();match=re.search(r'^define hidden fastcc void @loc_10075E20\(.*?^\}',text,re.M|re.S);assert match
cpu_type=next(line for line in text.splitlines() if line.startswith('%_cpu = type'))
out=R/'generated/integration-review/patch-self-entry';out.mkdir(exist_ok=True)
ir=cpu_type+'''
@ready = global i32 0
@observed = global i32 0
define i32 @qol_patch_ready() { %v = load i32, ptr @ready
 ret i32 %v
}
define hidden fastcc void @tribes_qol_unpatched_10075e20(%_cpu* %cpu) {
 store i32 1, ptr @observed
 ret void
}
define hidden fastcc void @loc_10061870(%_cpu* %cpu) {
 store i32 2, ptr @observed
 ret void
}
'''+match[0]+'''
define void @enter(ptr %cpu) { call fastcc void @loc_10075E20(ptr %cpu)
 ret void
}
'''
(out/'test.ll').write_text(ir)
subprocess.run(['clang','-target','arm64-apple-macos12','-O0','-Wno-override-module','-dynamiclib',str(out/'test.ll'),'-o',str(out/'test.dylib')],check=True)
lib=ctypes.CDLL(str(out/'test.dylib'));ready=ctypes.c_uint32.in_dll(lib,'ready');observed=ctypes.c_uint32.in_dll(lib,'observed');lib.enter.argtypes=[ctypes.c_void_p];rng=random.Random(25034)
for i in range(256):
 cpu=ctypes.create_string_buffer(bytes(rng.getrandbits(8) for _ in range(440)),440);before=cpu.raw;ready.value=i%2;observed.value=0;lib.enter(cpu);assert observed.value==1+ready.value and cpu.raw==before
(out/'result.json').write_text(json.dumps(dict(passed=True,cases=256,source_sha256=sha(source),binary_sha256=sha(out/'test.dylib'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Exact emitted self-entry wrapper routes to the original body before publication and the captured detour after publication, preserving every CPU byte. Target bodies are observers in this focused switch test.'),indent=2)+'\n');print('PASS: 256 self-entry route/CPU-preservation cases')
