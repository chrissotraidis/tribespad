#!/usr/bin/env python3
"""Lift three function-pointer targets misclassified as data in the base DLL lift."""
import hashlib,importlib.util,json,os,pathlib,re,shutil,subprocess
import pefile
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();source=R/'generated/srw/run-20261003T190924-ifc22';out=R/'generated/integration-review/pending-hook-bank/detours';out.mkdir(exist_ok=True)
plan=R/'generated/integration-review/pending-hook-bank/plan.json';m=json.loads(plan.read_text());native=json.loads((source/'native-diagnostic/result.json').read_text());assert sha(source/'IFC22.dll')==native['image_sha256']
spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
pe=pefile.PE(str(source/'IFC22.dll'));exports={e.name.decode():'loc_%X'%(0x10000000+e.address) for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name and not e.forwarder}
original=va.replace_names((source/'seg01_code.llinc').read_text(),exports);names=set(re.findall(r'^proc (\S+)',original,re.M));missing={r['detour_target'] for r in m['trampolines'] if 'loc_%X'%r['detour_target'] not in names};assert missing=={0x10007170,0x10010fb0,0x10026d70}
copied=[source/'IFC22.dll',source/'relocations.csv',source/'literal_fixups.csv',*source.glob('*.sci')]
for p in copied:shutil.copy2(p,out/p.name)
exclude=(out/'fixup_do_not_interpret_as_code.sci').read_text().splitlines();assert all('loc_%X'%x in exclude for x in missing)
(out/'fixup_do_not_interpret_as_code.sci').write_text('\n'.join(n for n in exclude if n not in {'loc_%X'%x for x in missing})+'\n')
with (out/'fixup_interpret_as_code.sci').open('a') as f:f.write(''.join('loc_%X\n'%x for x in sorted(missing)))
(out/'global_aliases.sci').write_text(''.join('loc_%X,loc_%X\n'%(x,x) for x in sorted(missing)))
srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text());assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256'];env=dict(os.environ,TRIBES_SRW_FULL_FLAGS='1',HALOPAD_SRW_DIAG='1')
# The base DLL has known diagnostic instructions elsewhere. Every selected new
# block below must be fully translated; no new diagnostic lowering is admitted.
with (out/'srw.log').open('w') as log:subprocess.run([str(srw),'IFC22.dll','ifc22.llasm'],cwd=out,env=env,stdout=log,stderr=log,timeout=120,check=True)
code=va.replace_names((out/'seg01_code.llinc').read_text(),exports)
blocks={z[1]:z[0] for z in re.finditer(r'^proc (\S+)[^\n]*\n(?:(?!^proc ).)*?^endp\s*$',code,re.M|re.S)};added=set(blocks)-names;assert {'loc_%X'%x for x in missing}<=added
selected='\n\n'.join(blocks[n] for n in sorted(added))+'\n';(out/'selected.llinc').write_text(selected)
# One exact original SQRTSS has no base SRW lowering. Use the separately
# checked scalar adapter; reject every other missing instruction.
assert pe.get_data(0xc597,4)==bytes.fromhex('f30f51c9')
needle=';sqrtss xmm1, xmm1\ncall halopad_trap_unimplemented 0x1000c597'
assert selected.count(needle)==1
selected=selected.replace(needle,';sqrtss xmm1, xmm1\ncall tb_sse_sqrtss $, 1, 1',1)
assert 'halopad_trap_unimplemented' not in selected,'New detour includes untranslated instructions; inspect selected.llinc'
refs=set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$',selected,re.M));external=refs-added;assert external<=names,external-names
values={n:r['guest_va'] for r in native['imports'] for n in [r['name'],r['sr_name']]};lines,stats=va.transform(selected.splitlines(),values)
(out/'code.llinc').write_text('\n'.join(lines)+'\n');(out/'extern.llinc').write_text((source/'native-diagnostic/extern.llinc').read_text()+'\nfuncv tb_sse_sqrtss $, dest, source\n'+''.join('proc %s external\n'%n for n in sorted(external)));(out/'macros.llinc').write_text('')
(out/'module.llasm').write_text('include llasm.llinc\ninclude tribes_mmx.llinc\ninclude tribes_sse.llinc\ninclude tribes_scan.llinc\ninclude tribes_x87.llinc\ninclude tribes_shift.llinc\ninclude tribes_atomic.llinc\ninclude extern.llinc\ninclude code.llinc\n')
with (out/'llasm.log').open('w') as log:subprocess.run([str(R/'generated/tools/llasm/llasm'),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(out/'module.llasm')],cwd=out,stdout=log,stderr=log,check=True)
procedures,_=va.postprocess(out/'raw.ll',out/'module.ll');assert set(procedures)==added
subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-c',str(out/'module.ll'),'-o',str(out/'module.o')],check=True)
report=dict(passed=True,published=False,detour_targets=sorted(missing),added=sorted(added),external=sorted(external),plan_sha256=sha(plan),module_sha256=sha(out/'module.ll'),script_sha256=sha(pathlib.Path(__file__).resolve()),source_sha256={str(p.relative_to(R)):sha(p) for p in copied+[source/'seg01_code.llinc',source/'native-diagnostic/extern.llinc',R/'scripts/prepare-va-model.py',R/'port/llasm-support/tribes_sqrtss.c']},scope='Original three registered detour functions and newly reached static blocks; all selected instructions translate without diagnostics. Runtime acceptance remains separate.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Compiled',len(added),'new detour blocks')
