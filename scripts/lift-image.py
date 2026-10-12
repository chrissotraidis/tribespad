#!/usr/bin/env python3
"""Run pinned SRW on the actual Tribes image. Diagnostic output is not a playable build."""
import argparse,datetime,hashlib,importlib.util,json,os,pathlib,shutil,subprocess,re
import pefile
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--module',choices=['tribes2','ifc22','mss32']+[r['module'] for r in json.loads((ROOT/'config/runtime/miles-codecs.json').read_text())],default='tribes2');p.add_argument('--diagnostic',action='store_true');p.add_argument('--full-flags',action='store_true');p.add_argument('--flags-hints',type=pathlib.Path);p.add_argument('--tribes-tools',action='store_true');p.add_argument('--native-zlib-routes',action='store_true',help='IFC22 only: pinned native optimized Adler integration');p.add_argument('--timeout',type=int,default=600);a=p.parse_args()
lock=json.loads((ROOT/'toolchains.lock.json').read_text())
for name in ['SRW/SRW.exe','llasm/llasm']:
 if hashlib.sha256((ROOT/'generated/tools'/name).read_bytes()).hexdigest()!=lock['artifacts'][name]:raise SystemExit('Translator identity mismatch: '+name)
srw=ROOT/'generated/tools/SRW/SRW.exe'
if a.tribes_tools:
 build=json.loads((ROOT/'generated/tools/SRW-tribes/tribes-build.json').read_text())
 assert build['patch_sha256']==lock['tribes_extensions']['patch_sha256']
 assert hashlib.sha256((ROOT/'scripts/patches/srw-tribes.patch').read_bytes()).hexdigest()==build['patch_sha256']
 srw=ROOT/'generated/tools/SRW-tribes/SRW.exe'
 assert hashlib.sha256(srw.read_bytes()).hexdigest()==build['srw_sha256']
manifest=json.loads((ROOT/'inputs-manifest.json').read_text())
codec_paths={r['module']:r['path'] for r in json.loads((ROOT/'config/runtime/miles-codecs.json').read_text())}
relpath={**codec_paths,'tribes2':'ref/control/Tribes2.exe','ifc22':'ref/qol/IFC22.dll','mss32':'ref/qol/Mss32.dll'}[a.module]
exe=ROOT/relpath
expected=next(m['sha256'] for m in manifest['modules'] if m['path']==relpath)
if hashlib.sha256(exe.read_bytes()).hexdigest()!=expected:raise SystemExit('Tribes2.exe identity mismatch')
analysis=ROOT/'generated/analysis'/a.module;work=ROOT/'generated/srw'/datetime.datetime.now(datetime.timezone.utc).strftime('run-%Y%m%dT%H%M%S-'+a.module)
work.mkdir(parents=True);shutil.copy2(exe,work/exe.name);shutil.copy2(analysis/'relocations.csv',work/'relocations.csv')
for f in (analysis/'srw').glob('*'):shutil.copy2(f,work/f.name)
code_seeds=None
if a.module=='mss32':
 seed_path=ROOT/'config/runtime/miles-mixer.json';code_seeds=json.loads(seed_path.read_text());assert code_seeds['image_sha256']==expected
 entries={'loc_%X'%pc for pc in code_seeds['entries'].values()}
 code=work/'fixup_interpret_as_code.sci';data=work/'fixup_do_not_interpret_as_code.sci'
 code.write_text('\n'.join(sorted(set(code.read_text().splitlines())|entries))+'\n')
 data.write_text('\n'.join(n for n in data.read_text().splitlines() if n not in entries)+'\n')
 code_seeds['source_sha256']=hashlib.sha256(seed_path.read_bytes()).hexdigest()
native_zlib=None
if a.native_zlib_routes:
 # Diagnostic runs retain the same verified replacement routes so their gap
 # census reflects the selected product build. They still emit explicit traps
 # and remain diagnostic in result.json; only strict runs establish translation.
 assert a.module=='ifc22' and a.tribes_tools and a.full_flags
 spec=importlib.util.spec_from_file_location('native_zlib_routes',ROOT/'scripts/prepare-qol-zlib-routes.py');routes=importlib.util.module_from_spec(spec);spec.loader.exec_module(routes)
 native_zlib=routes.prepare(work/'native-zlib-routes')
 assert not (work/'instruction_replacements.sci').exists(), 'Existing instruction replacements require explicit merge review'
 shutil.copy2(work/'native-zlib-routes/instruction_replacements.sci',work/'instruction_replacements.sci')
if a.flags_hints:shutil.copy2(a.flags_hints,work/'instruction_flags.sci')
env=dict(os.environ)
if a.diagnostic:env['HALOPAD_SRW_DIAG']='1'
if a.full_flags:env['TRIBES_SRW_FULL_FLAGS']='1'
with (work/'stdout.log').open('w') as out,(work/'stderr.log').open('w') as err:
 try:r=subprocess.run([str(srw),exe.name,a.module+'.llasm'],cwd=work,env=env,stdout=out,stderr=err,timeout=a.timeout);code=r.returncode
 except subprocess.TimeoutExpired:code='timeout'
if a.tribes_tools and code==0:
 output=work/(a.module+'.llasm')
 body=output.read_text()
 includes=''.join('include '+n+'\n' for n in ['tribes_mmx.llinc','tribes_sse.llinc','tribes_scan.llinc','tribes_x87.llinc','tribes_shift.llinc','tribes_atomic.llinc'])
 output.write_text(includes+body)
 declared=set(re.findall(r'^proc (\S+) external',body,re.M))
 pe=pefile.PE(str(exe))
 imports={imp.name.decode() for d in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[]) for imp in d.imports if imp.name}
 extra=[f'proc {n} external' for n in sorted(imports-declared)]
 if native_zlib:extra+=['proc '+name+' external' for _,name in native_zlib['entries']]
 extra+=['funcv halopad_trap_unimplemented address']
 (work/'extern.llinc').write_text('\n'.join(extra)+'\n')
 (work/'macros.llinc').write_text('')
(work/'result.json').write_text(json.dumps({'code_seeds':code_seeds,'native_zlib_routes':native_zlib,'exit':code,'diagnostic':a.diagnostic,'full_flags':a.full_flags,'image_sha256':expected,'srw_sha256':hashlib.sha256(srw.read_bytes()).hexdigest(),'playable':False,'flags_hints_sha256':hashlib.sha256(a.flags_hints.read_bytes()).hexdigest() if a.flags_hints else None},indent=2)+'\n')
print(work);print((work/'stderr.log').read_text()[-3000:]);raise SystemExit(0 if code==0 else 1)
