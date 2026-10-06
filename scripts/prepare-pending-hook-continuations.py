#!/usr/bin/env python3
"""Expose only missing original continuations needed by the live pending bank."""
import hashlib,importlib.util,json,os,pathlib,re,shutil,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();source=R/'generated/integration-review/string-patched-engine';out=R/'generated/integration-review/pending-hook-bank/continuations';out.mkdir(exist_ok=True)
plan=R/'generated/integration-review/pending-hook-bank/plan.json';m=json.loads(plan.read_text());original=(source/'seg01_code.llinc').read_text();names=set(re.findall(r'^proc (\S+)',original,re.M));missing={r['continuation'] for r in m['trampolines'] if 'loc_%X'%r['continuation'] not in names};assert len(missing)==17
for p in [source/'Tribes2.exe',source/'relocations.csv',source/'literal_fixups.csv',*source.glob('*.sci')]:shutil.copy2(p,out/p.name)
(out/'global_aliases.sci').write_text((source/'global_aliases.sci').read_text()+''.join('loc_%X,loc_%X\n'%(x,x) for x in sorted(missing)))
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];env={k:v for k,v in os.environ.items() if k!='HALOPAD_SRW_DIAG'};env['TRIBES_SRW_FULL_FLAGS']='1'
with (out/'srw.log').open('w') as log:run=subprocess.run([str(srw),'Tribes2.exe','tribes2.llasm'],cwd=out,env=env,stdout=log,stderr=log,timeout=120)
assert not run.returncode
blocks={m[1]:m[0] for m in re.finditer(r'^proc (\S+)[^\n]*\n(?:(?!^proc ).)*?^endp\s*$',(out/'seg01_code.llinc').read_text(),re.M|re.S)}
added=set(blocks)-names;assert added=={'loc_%X'%x for x in missing},added
selected='\n\n'.join(blocks[n] for n in sorted(added))+'\n';assert 'halopad_trap_unimplemented' not in selected
refs=set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$',selected,re.M));external=refs-added;assert external<=names,external-names
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va);lines,stats=va.transform(selected.splitlines())
(out/'code.llinc').write_text('\n'.join(lines)+'\n');(out/'extern.llinc').write_text((source/'va/extern.llinc').read_text()+''.join('proc %s external\n'%n for n in sorted(external)));(out/'macros.llinc').write_text('')
(out/'module.llasm').write_text('include llasm.llinc\ninclude tribes_mmx.llinc\ninclude tribes_sse.llinc\ninclude tribes_scan.llinc\ninclude tribes_x87.llinc\ninclude tribes_shift.llinc\ninclude tribes_atomic.llinc\ninclude extern.llinc\ninclude code.llinc\n')
with (out/'llasm.log').open('w') as log:subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'module.llasm')],cwd=out,stdout=log,stderr=log,check=True)
procedures,_=va.postprocess(out/'raw.ll',out/'module.ll');assert set(procedures)==added
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-c',str(out/'module.ll'),'-o',str(out/'module.o')],check=True)
report=dict(passed=True,published=False,added=sorted(added),external=sorted(external),plan_sha256=sha(plan),module_sha256=sha(out/'module.ll'),script_sha256=sha(pathlib.Path(__file__).resolve()),source_sha256={str(p.relative_to(R)):sha(p) for p in [source/'Tribes2.exe',source/'seg01_code.llinc',source/'va/extern.llinc',R/'scripts/prepare-va-model.py']},scope='Strictly relifted17 missing original continuation entries; no patch admission or game acceptance.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Compiled17 missing continuations')
