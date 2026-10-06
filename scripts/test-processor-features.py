#!/usr/bin/env python3
"""Validate Windows guest feature queries against the declared CPU profile."""
import ctypes,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/processor-features';out.mkdir(exist_ok=True)
sources=[R/'port/core/guest_cpu.c',R/'port/core/guest_processor_features.c'];lib=out/'features.dylib';subprocess.run(['clang','-arch','arm64','-O2','-DPTROFS_64BIT','-dynamiclib','-Wall','-Wextra','-Werror']+list(map(str,sources))+['-o',str(lib)],check=True)
fn=ctypes.CDLL(str(lib)).tribes_processor_feature_present;fn.argtypes=[ctypes.c_uint32];fn.restype=ctypes.c_uint32
profile=json.loads((R/'config/runtime/cpu.json').read_text());checks=[]
for code,name in [(3,'MMX'),(6,'SSE'),(8,'TSC'),(10,'SSE2'),(13,'SSE3')]:
    result=fn(code);assert result==int(name in profile['features']),(name,result);checks.append(dict(feature=code,name=name,present=bool(result)))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps(dict(passed=True,checks=checks,binary_sha256=sha(lib),source_sha256={str(p.relative_to(R)):sha(p) for p in sources+[R/'port/core/guest_processor_features.h',R/'config/runtime/cpu.json',pathlib.Path(__file__).resolve()]},scope='Declared guest CPUID profile versus five supported Windows PF queries; no Apple hardware feature advertisement.'),indent=2)+'\n')
print('PASS: five Windows feature queries match the declared guest CPU profile')
