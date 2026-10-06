#!/usr/bin/env python3
"""Exercise actual native CoreAudio and OpenAL positional sample rendering."""
import hashlib,json,os,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/openal-soft/macos';meta=json.loads((out/'result.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();library=R/meta['library'];assert sha(library)==meta['library_sha256']
source=R/'tests/openal_native_test.c';exe=out/'native-test'
cmd=['clang','-isysroot',meta['sdk'],'-target','arm64-apple-macos12','-O2','-DAL_LIBTYPE_STATIC','-I',str(R/'ref/openal-soft/include'),str(source),str(library),'-lc++','-framework','CoreAudio','-framework','AudioUnit','-framework','ApplicationServices','-framework','AudioToolbox','-o',str(exe)]
r=subprocess.run(cmd,capture_output=True,text=True);(out/'test-build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
env=dict(os.environ,ALSOFT_DRIVERS='core',ALSOFT_LOGLEVEL='2',ALSOFT_CONF='/dev/null')
r=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=20);(out/'test-run.log').write_text(r.stdout+r.stderr);assert not r.returncode,(r.returncode,r.stdout,r.stderr)
report=dict(passed=True,revision=meta['revision'],library_sha256=sha(library),test_source_sha256=sha(source),binary_sha256=sha(exe),stdout=r.stdout,scope='Pinned native library: actual CoreAudio output/cursor advancement and real loopback stereo positional PCM/distance attenuation. No guest ABI, original engine, simulator or gameplay acceptance.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
