#!/usr/bin/env python3
"""Verify the frequency policy against explicit native syscall outcomes."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/registry-frequency';out.mkdir(parents=True,exist_ok=True)
sources=[R/'port/core/guest_registry.c',R/'tests/registry_frequency_test.c']
exe=out/'test'
subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-Dsysctlbyname=fixture_sysctlbyname',*map(str,sources),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,check=True)
assert run.stdout.startswith('PASS:') and not run.stderr
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
sources += [R/'port/core/guest_registry.h',R/'port/core/cpu_metadata_profile.h',R/'config/runtime/cpu-metadata.json',pathlib.Path(__file__).resolve()]
(out/'result.json').write_text(json.dumps(dict(passed=True,stdout=run.stdout,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources}),indent=2)+'\n')
print(run.stdout.strip())
