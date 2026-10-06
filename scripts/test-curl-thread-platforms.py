#!/usr/bin/env python3
"""Compile production curl runtime/allocator on device and Simulator."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/curl-threads/platforms';out.mkdir(parents=True,exist_ok=True);rows=[]
for platform,sdkname,target in [('device','iphoneos','arm64-apple-ios16'),('simulator','iphonesimulator','arm64-apple-ios16-simulator')]:
 sdk=subprocess.check_output(['xcrun','--sdk',sdkname,'--show-sdk-path'],text=True).strip();include=R/'generated/http-backend'/platform/'install/include';backend=include.parent.parent/'result.json';d=json.loads(backend.read_text())
 for name,h in d['libraries'].items():assert sha(R/name)==h
 for name in ['guest_curl','guest_curl_allocator']:
  source=R/'port/core'/(name+'.c');obj=out/(platform+'-'+name+'.o')
  subprocess.run(['clang','-target',target,'-isysroot',sdk,'-O2','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-I',str(include),'-c',str(source),'-o',str(obj)],check=True)
  rows.append(dict(platform=platform,source=str(source.relative_to(R)),source_sha256=sha(source),object=str(obj.relative_to(R)),object_sha256=sha(obj),backend_sha256=sha(backend)))
(out/'result.json').write_text(json.dumps(dict(passed=True,execution_tested=False,rows=rows,script_sha256=sha(pathlib.Path(__file__).resolve()),headers_sha256={str(p.relative_to(R)):sha(p) for p in [R/'port/core/guest_curl.h',R/'port/core/guest_curl_allocator.h',R/'port/core/guest_filesystem.h']}),indent=2)+'\n');print('PASS: production curl threading/transfer units compile for iOS device and Simulator')
