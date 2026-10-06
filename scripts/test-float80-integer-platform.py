#!/usr/bin/env python3
"""Execute x86-derived integer vectors natively or in the sole booted simulator."""
import argparse,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--simulator');a=p.parse_args();platform='simulator' if a.simulator else 'macos'
device=None
if a.simulator:
 inventory=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','-j'],text=True));booted=[d for ds in inventory['devices'].values() for d in ds if d['state']=='Booted']
 assert len(booted)==1 and booted[0]['udid']==a.simulator,'Exactly the requested simulator must be booted'
 device=booted[0]
reference=R/'generated/softfloat/integer-stores';proof=json.loads((reference/'result.json').read_text());assert proof['passed']
for n,h in proof['sources_sha256'].items():assert sha(R/n)==h,n
cases=reference/'reference-cases.txt';assert sha(cases)==proof['reference_cases_sha256'];assert len(cases.read_text().splitlines())==proof['cases']==33225
out=reference/platform;out.mkdir(exist_ok=True)
target='arm64-apple-ios16-simulator' if a.simulator else 'arm64-apple-macos12';sdk_name='iphonesimulator' if a.simulator else 'macosx';sdk=subprocess.check_output(['xcrun','--sdk',sdk_name,'--show-sdk-path'],text=True).strip()
bochs=R/'generated/bochs-math';bochs=bochs/'simulator' if a.simulator else bochs
meta=json.loads((bochs/'result.json').read_text());assert sha(R/'port/bochs-support/math_bridge.cc')==meta['bridge_sha256'] and sha(bochs/'math_bridge.o')==meta['native_bridge_object_sha256'] and sha(bochs/'libbochs-math.a')==meta['library_sha256']
sf=R/'generated/softfloat'/platform;sfmeta=json.loads((sf/'result.json').read_text());assert sha(sf/'softfloat.a')==sfmeta['library_sha256']
sources=[R/n for n in ['tests/float80_integer_component.c','tests/x87_result_slot.c','port/core/ext80.c','port/llasm-support/tribes_float80.c']];objects=[]
flags=['-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(R/'ref/softfloat/SoftFloat-3e/source/include')]
for src in sources:
 obj=out/(src.stem+'.o');subprocess.run(['clang',*flags,'-Wall','-Wextra','-Werror','-c',str(src),'-o',str(obj)],check=True);objects.append(obj)
exe=out/'test';libs=[sf/'softfloat.a',bochs/'math_bridge.o',bochs/'libbochs-math.a']
subprocess.run(['clang++',*flags,*map(str,objects+libs),'-o',str(exe)],check=True)
version=subprocess.check_output(['xcrun','vtool','-show-build',str(exe)],text=True);assert ('platform IOSSIMULATOR\n' if a.simulator else 'platform MACOS\n') in version
command=(['xcrun','simctl','spawn',a.simulator] if a.simulator else [])+[str(exe),str(cases)]
run=subprocess.run(command,capture_output=True,text=True,timeout=30);(out/'stdout.log').write_text(run.stdout);(out/'stderr.log').write_text(run.stderr);assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
report=dict(passed=True,cases=33225,platform=platform,target=target,device=device,binary_sha256=sha(exe),reference_sha256=sha(reference/'result.json'),vectors_sha256=sha(cases),sources_sha256={str(f.relative_to(R)):sha(f) for f in sources+libs+[pathlib.Path(__file__).resolve()]},command=command,scope='ARM64 command-line integer-store component against independent x86-derived vectors. Numeric/status/stack/CPU preservation; test-only wide-result slot. No app launch, physical-device run or gameplay acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(run.stdout,end='')
