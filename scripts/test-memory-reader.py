#!/usr/bin/env python3
"""Exercise production permission barrier races, normally and under TSan."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/integration-review/memory-reader';out.mkdir(parents=True,exist_ok=True)
(out/'result.json').write_text('{"passed":false,"state":"running"}\n')
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip();runs=[]
for mode in ['normal','tsan']:
 exe=out/('test-'+mode)
 subprocess.run(['clang','-O2','-g','-isysroot',sdk,'-Wall','-Wextra','-Werror',*(['-fsanitize=thread'] if mode=='tsan' else []),str(R/'tests/memory_reader_test.c'),'-o',str(exe)],check=True)
 p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=90)
 (out/(mode+'.log')).write_text(p.stdout+p.stderr);assert p.returncode==0 and p.stdout.startswith('PASS:') and not p.stderr,(p.returncode,p.stdout,p.stderr)
 print(mode,p.stdout.strip());runs.append(dict(mode=mode,stdout=p.stdout))
paths=['port/core/guest_memory_checks.c','tests/memory_reader_test.c','scripts/test-memory-reader.py']
(out/'result.json').write_text(json.dumps(dict(passed=True,runs=runs,sources_sha256={p:hashlib.sha256((R/p).read_bytes()).hexdigest() for p in paths}),indent=2)+'\n')
