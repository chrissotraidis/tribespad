#!/usr/bin/env python3
"""Compile pinned pure floating math without the Bochs execution engine."""
import argparse,concurrent.futures,hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--platform',choices=['macos','device','simulator'],default='macos');a=p.parse_args()
lock=next(s for s in json.loads((R/'dependencies.lock.json').read_text())['sources'] if s['name']=='bochs-extended80-candidate')
root=R/lock['path'];out=R/'generated/bochs-math';out=out/a.platform if a.platform!='macos' else out;out.mkdir(parents=True,exist_ok=True)
assert subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()==lock['revision']
for name,digest in lock['files'].items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,name+' changed'
source=[root/name for name in lock['files'] if name.endswith('.cc')]
sdk={'macos':'macosx','device':'iphoneos','simulator':'iphonesimulator'}[a.platform]
target={'macos':'arm64-apple-macos12','device':'arm64-apple-ios16','simulator':'arm64-apple-ios16-simulator'}[a.platform]
sdkpath=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
flags=['clang++','-target',target,'-isysroot',sdkpath,'-std=c++17','-O2','-I',str(R/'port/bochs-support'),'-I',str(root/'bochs/cpu/softfloat3e/include'),'-I',str(root/'bochs/cpu'),'-I',str(root/'bochs/cpu/fpu')]
def compile_one(path):
    obj=out/(path.parent.name+'-'+path.stem+'.o')
    run=subprocess.run(flags+['-c',str(path),'-o',str(obj)],text=True,capture_output=True)
    return obj,run.returncode,run.stdout+run.stderr
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    results=list(pool.map(compile_one,source))
(out/'build.log').write_text(''.join(str(obj)+'\n'+log for obj,_,log in results))
errors=[str(obj) for obj,code,_ in results if code]
report={'revision':lock['revision'],'platform':a.platform,'target':target,'objects':len(results),'errors':errors,'cpu_interpreter_compiled':False,'engine_integrated':False}
if not errors:
    (out/'libbochs-math.a').unlink(missing_ok=True)
    subprocess.run(['ar','rcs',str(out/'libbochs-math.a'),*[str(obj) for obj,_,_ in results]],check=True)
    report['library_sha256']=hashlib.sha256((out/'libbochs-math.a').read_bytes()).hexdigest()
    bridge=R/'port/bochs-support/math_bridge.cc'
    subprocess.run(flags+['-c',str(bridge),'-o',str(out/'math_bridge.o')],check=True)
    if a.platform=='macos':subprocess.run(flags+['-dynamiclib',str(out/'math_bridge.o'),str(out/'libbochs-math.a'),'-o',str(out/'libmath.dylib')],check=True)
    report['bridge_sha256']=hashlib.sha256(bridge.read_bytes()).hexdigest()
    report['config_sha256']=hashlib.sha256((R/'port/bochs-support/config.h').read_bytes()).hexdigest()
    report['native_bridge_object_sha256']=hashlib.sha256((out/'math_bridge.o').read_bytes()).hexdigest()
    if a.platform=='macos':report['native_bridge_sha256']=hashlib.sha256((out/'libmath.dylib').read_bytes()).hexdigest()
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if errors:raise SystemExit('Math compilation failed; see '+str(out/'build.log'))
