#!/usr/bin/env python3
"""Run checked byte atomic contention and fault cases on the sole booted simulator."""
import argparse,hashlib,json,pathlib,subprocess
from checked_support import compile_support
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('device');a=ap.parse_args()
d=json.loads(subprocess.check_output(['xcrun','simctl','list','devices','booted','--json'],text=True));assert [x['udid'] for rows in d['devices'].values() for x in rows if x['state']=='Booted']==[a.device]
root=R/'generated/integration-review/locked-cmpxchg-byte';out=root/a.device;out.mkdir(exist_ok=True)
sources=[root/'blocks.ll',R/'port/llasm-support/tribes_atomic.c'];test=R/'tests/locked_cmpxchg_byte_test.c';guard=R/'port/core/guest_memory_checks.c'
sdk=subprocess.check_output(['xcrun','--sdk','iphonesimulator','--show-sdk-path'],text=True).strip();target='arm64-apple-ios16-simulator'
objects,checked=compile_support(sources,out/'checked',['-DPTROFS_64BIT'],target=target,sdk_name='iphonesimulator');exe=out/'test'
subprocess.run(['clang','-target',target,'-isysroot',sdk,'-O1','-g','-DPTROFS_64BIT',str(test),str(guard),*objects,'-o',str(exe)],check=True)
command=['xcrun','simctl','spawn',a.device,str(exe)];run=subprocess.run(command+['no-fork'],capture_output=True,text=True,timeout=60);(out/'run.log').write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:'),(run.returncode,run.stdout,run.stderr)
for n in range(5):
 f=subprocess.run(command+['f'+str(n)],capture_output=True,text=True,timeout=30);(out/('fault-%d.log'%n)).write_text(f.stdout+f.stderr);assert f.returncode==134 and 'checked guest memory violation' in f.stderr,(n,f.returncode,f.stderr)
device_sdk=subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip();obj=out/'device-atomic.o'
subprocess.run(['clang','-target','arm64-apple-ios16','-isysroot',device_sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-c',str(sources[1]),'-o',str(obj)],check=True)
report=dict(passed=True,simulator_device=a.device,binary_sha256=sha(exe),device_object_sha256=sha(obj),device_execution=False,checked_sha256=checked,sources_sha256={str(p.relative_to(R)):sha(p) for p in sources+[test,guard,pathlib.Path(__file__).resolve()]},faults=5,stdout=run.stdout,scope='Actual emitted byte CAS on sole iPad Simulator:80000 contended lock acquisitions and five protected-memory failures. Device compile only; no physical-device or whole-engine acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',run.stdout.strip(),'; five Simulator faults; device compilation')
