#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];U=R/'generated/tools/SRW-tribes/udis86-1.7.2';out=R/'generated/sse/legacy-movsd-decoder';out.mkdir(exist_ok=True)
sources=[R/'tests/legacy_movsd_decoder_test.c']+sorted((U/'libudis86').glob('*.c'));exe=out/'test'
subprocess.run(['clang','-O2','-Wall','-Wextra','-Werror','-DHAVE_STRING_H=1','-I',str(U),*map(str,sources),'-o',str(exe)],check=True)
run=subprocess.run([str(exe)],capture_output=True,text=True,check=True);assert run.stdout.startswith('PASS:') and not run.stderr
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();(out/'result.json').write_text(json.dumps(dict(passed=True,output=run.stdout,binary_sha256=sha(exe),sources_sha256={str(p.relative_to(R)):sha(p) for p in sources}),indent=2)+'\n');print(run.stdout,end='')
