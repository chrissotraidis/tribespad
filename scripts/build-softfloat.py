#!/usr/bin/env python3
"""Build pinned SoftFloat 3e natively, with Intel NaNs and per-thread state."""
import argparse, hashlib, json, pathlib, shutil, subprocess, urllib.request, zipfile

R = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('--platform', choices=['macos', 'device', 'simulator'], default='macos')
a = p.parse_args()
source = next(s for s in json.loads((R/'dependencies.lock.json').read_text())['sources'] if s['name']=='softfloat')
archive = R/'ref/softfloat/SoftFloat-3e.zip'
archive.parent.mkdir(parents=True, exist_ok=True)
if not archive.exists():
    urllib.request.urlretrieve(source['url'], archive)
assert hashlib.sha256(archive.read_bytes()).hexdigest() == source['sha256'], 'SoftFloat archive identity changed'
# Re-extract from the verified archive; never trust a previously edited source tree.
with zipfile.ZipFile(archive) as z:
    for info in z.infolist():
        name = pathlib.PurePosixPath(info.filename)
        assert not name.is_absolute() and '..' not in name.parts
    z.extractall(archive.parent)
root = R/source['path']
out = R/'generated/softfloat'/a.platform
out.mkdir(parents=True, exist_ok=True)
shutil.copyfile(root/'build/Linux-x86_64-GCC/Makefile', out/'Makefile')
shutil.copyfile(root/'build/Linux-x86_64-GCC/platform.h', out/'platform.h')
# This platform header uses only portable little-endian integer/Clang intrinsics.
sdk = {'macos':'macosx','device':'iphoneos','simulator':'iphonesimulator'}[a.platform]
target = {'macos':'arm64-apple-macos12','device':'arm64-apple-ios16','simulator':'arm64-apple-ios16-simulator'}[a.platform]
sdkpath = subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'], text=True).strip()
flags = f'-target {target} -isysroot {sdkpath} -std=c11 -DSOFTFLOAT_FAST_INT64 -DTHREAD_LOCAL=_Thread_local -DINLINE_LEVEL=5 -DSOFTFLOAT_FAST_DIV32TO16 -DSOFTFLOAT_FAST_DIV64TO32'
command = ['make','-j4',f'SOURCE_DIR={root}/source','SPECIALIZE_TYPE=8086',f'COMPILE_C=clang -c {flags} -I. -I{root}/source/8086 -I{root}/source/include -O2 -o $@']
with (out/'build.log').open('w') as log:
    subprocess.run(command, cwd=out, stdout=log, stderr=subprocess.STDOUT, check=True)
adapter_command = ['clang','-target',target,'-isysroot',sdkpath,'-std=c11','-O2','-Wall','-Wextra','-Werror','-DSOFTFLOAT_FAST_INT64','-DTHREAD_LOCAL=_Thread_local','-I',str(root/'source/include'),'-c',str(R/'port/core/ext80.c'),'-o',str(out/'ext80.o')]
subprocess.run(adapter_command, check=True)
runtime_command = adapter_command.copy()
runtime_command[runtime_command.index(str(R/'port/core/ext80.c'))] = str(R/'port/llasm-support/tribes_float80.c')
runtime_command[-1] = str(out/'tribes_float80.o')
runtime_command.insert(1,'-DPTROFS_64BIT')
subprocess.run(runtime_command, check=True)
report = {'source_sha256':source['sha256'], 'platform':a.platform,'target':target,'specialization':'8086','thread_local':True,'library_sha256':hashlib.sha256((out/'softfloat.a').read_bytes()).hexdigest(),'command':command,'engine_integrated':False}
report['adapter_sha256'] = hashlib.sha256((out/'ext80.o').read_bytes()).hexdigest()
report['adapter_command'] = adapter_command
report['runtime_sha256'] = hashlib.sha256((out/'tribes_float80.o').read_bytes()).hexdigest()
report['runtime_command'] = runtime_command
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
