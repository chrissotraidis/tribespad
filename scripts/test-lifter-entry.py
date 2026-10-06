#!/usr/bin/env python3
"""PE entry with no alias/export/relocation must survive LLASM discovery."""
import hashlib,json,os,pathlib,subprocess
from sse_instruction_fixture import fixture
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/entry-seed';out.mkdir(exist_ok=True)
srw=R/'generated/tools/SRW-tribes/SRW.exe';image=R/'ref/product/IFC22.dll'
fixture(out,[dict(bytes_hex='5589e5837d0c015d')],image,srw)
(out/'global_aliases.sci').unlink()
run=subprocess.run([str(srw),'fixture.exe','entry.llasm'],cwd=out,capture_output=True,text=True,env={k:v for k,v in {**os.environ,'TRIBES_SRW_FULL_FLAGS':'1'}.items() if k!='HALOPAD_SRW_DIAG'})
assert run.returncode==0,run.stderr
code=(out/'seg01_code.llinc').read_text();assert 'proc loc_10001000\n' in code and 'PUSH ebp' in code and ';cmp dword [ebp+0xc], 0x1' in code and ';ret\n' in code,code
for name in ['extern.llinc','macros.llinc']:(out/name).write_text('')
subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'entry.ll'),str(out/'entry.llasm')],cwd=out,check=True,capture_output=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=dict(pass_=True,fixture_sha256=sha(out/'fixture.exe'),srw_sha256=sha(srw),compiler_source_sha256=sha(R/'generated/tools/SRW-tribes/SR_basic.c'),driver_sha256=sha(pathlib.Path(__file__).resolve()),scope='Unreferenced PE entry emitted without aliases, exports or relocations; whole module and native startup checked separately.')
report['pass']=report.pop('pass_');(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: PE entry discovered without aliases/exports/relocations')
