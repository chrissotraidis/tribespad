#!/usr/bin/env python3
"""Reproduce the host translator's finite VEX decoder contract checks."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
root=R/'generated/tools/SRW-tribes/udis86-1.7.2';out=R/'generated/sse/vmovss-decoder';out.mkdir(parents=True,exist_ok=True)
report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n')
build=json.loads((root.parent/'tribes-build.json').read_text());lock=json.loads((R/'toolchains.lock.json').read_text())
assert build['patch_sha256']==lock['tribes_extensions']['patch_sha256']==sha(R/'scripts/patches/srw-tribes.patch')
source=R/'tests/vmovss_decoder_test.c';sources=[source]+sorted((root/'libudis86').glob('*.c'));exe=out/'test'
cmd=['clang','-O2','-Wall','-DHAVE_STRING_H=1','-I',str(root)]+list(map(str,sources))+['-o',str(exe)]
run=subprocess.run(cmd,capture_output=True,text=True);(out/'compile.log').write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
run=subprocess.run([str(exe)],capture_output=True,text=True);(out/'run.log').write_text(run.stdout+run.stderr)
assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(run.returncode,run.stdout,run.stderr)
paths=sources+sorted((root/'libudis86').glob('*.h'))+[root/'udis86.h',pathlib.Path(__file__).resolve()]
report={'state':'finished','pass':True,'sources_sha256':{str(p.relative_to(R)):sha(p) for p in paths},'patch_sha256':build['patch_sha256'],'binary_sha256':sha(exe),'command':cmd,'output':run.stdout.strip(),'scope':'Host IA-32 decoder byte/operand/reset/truncation/legacy distinction checks; emitted AOT behavior is verified separately'}
report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
