"""Compile the reached original virtual-method closure missed by data seeds."""
import hashlib,importlib.util,json,os,pathlib,re,shutil,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
PC=0x10007d00;SQRT=0x1000c597
def translate_sqrt(text):
 import pefile
 image=R/'ref/qol/IFC22.dll';assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 assert pefile.PE(str(image)).get_data(SQRT-0x10000000,4)==bytes.fromhex('f30f51c9')
 needle=f'call void @halopad_trap_unimplemented(i32 {SQRT}) nounwind'
 assert text.count(needle)==1 and '@tb_sse_sqrtss' not in text
 return text.replace(needle,'call void @tb_sse_sqrtss(%_cpu* %cpu, i32 1, i32 1)',1)+'\ndeclare void @tb_sse_sqrtss(%_cpu*, i32, i32)\n'
def prepare(native,out,verify_test=True):
 if verify_test:
  proof=json.loads((R/'generated/integration-review/mission-entry-test/result.json').read_text());assert proof['passed']
  for n,h in proof['sources_sha256'].items():assert sha(R/n)==h,n
 source=native.parent;path=out/'mission-entry';path.mkdir(exist_ok=True)
 image=source/'IFC22.dll';assert sha(image)=='eaf5a42d301cc64a6e526aa49a82dbaefe655ed1ac0ba67939d0bab2fb6ab607'
 for p in [image,source/'relocations.csv',source/'literal_fixups.csv',*source.glob('*.sci')]:shutil.copy2(p,path/p.name)
 name=f'loc_{PC:X}';data=path/'fixup_do_not_interpret_as_code.sci';lines=data.read_text().splitlines();assert lines.count(name)==1
 data.write_text('\n'.join(n for n in lines if n!=name)+'\n');code=path/'fixup_interpret_as_code.sci';assert name not in code.read_text().splitlines();code.write_text(code.read_text()+name+'\n')
 srw=R/'generated/tools/SRW-tribes/SRW.exe';build=json.loads(srw.with_name('tribes-build.json').read_text())
 assert sha(srw)==build['srw_sha256'] and sha(R/'scripts/patches/srw-tribes.patch')==build['patch_sha256']
 env=dict(os.environ,HALOPAD_SRW_DIAG='1',TRIBES_SRW_FULL_FLAGS='1')
 with (path/'srw.log').open('w') as log:subprocess.run([str(srw),'IFC22.dll','ifc22.llasm'],cwd=path,env=env,stdout=log,stderr=log,check=True,timeout=120)
 pattern=r'^proc (\S+)[^\n]*\n(?:(?!^proc ).)*?^endp\s*$'
 before={m[1]:m[0] for m in re.finditer(pattern,(source/'seg01_code.llinc').read_text(),re.M|re.S)}
 after={m[1]:m[0] for m in re.finditer(pattern,(path/'seg01_code.llinc').read_text(),re.M|re.S)}
 assert not(set(before)-set(after)) and all(before[n]==after[n] for n in before)
 discovered=set(after)-set(before);assert len(discovered)==86 and name in discovered
 pending=R/'generated/integration-review/pending-hook-bank/detours'
 owner=json.loads((pending/'result.json').read_text());assert owner['passed'] and owner['module_sha256']==sha(pending/'module.ll')
 reused=discovered&set(owner['added']);assert reused=={'loc_1000C4F0','loc_1000C546','loc_1000C57D','loc_1000C5A0','loc_1000C5AC'}
 prior={m[1]:m[0] for m in re.finditer(pattern,(pending/'selected.llinc').read_text(),re.M|re.S)}
 assert all(after[n].strip()==prior[n].strip() for n in reused)
 added=discovered-reused
 selected='\n\n'.join(after[n] for n in sorted(added))+'\n'
 assert 'halopad_trap_unimplemented' not in selected
 refs=set(re.findall(r'^\s*(?:tcall|ctcallz|ctcallnz)\s+.*?\b(loc_[0-9A-F]+)\s*$',selected,re.M));external=refs-added;assert external<=set(before)|reused,external-(set(before)|reused)
 spec=importlib.util.spec_from_file_location('va',R/'scripts/prepare-va-model.py');va=importlib.util.module_from_spec(spec);spec.loader.exec_module(va)
 meta=json.loads((native/'result.json').read_text());values={n:row['guest_va'] for row in meta['imports'] for n in [row['name'],row['sr_name']]}
 transformed,stats=va.transform(selected.splitlines(),values);(path/'code.llinc').write_text('\n'.join(transformed)+'\n')
 (path/'extern.llinc').write_text((native/'extern.llinc').read_text()+''.join(f'proc {n} external\n' for n in sorted(external)))
 (path/'macros.llinc').write_text('')
 (path/'module.llasm').write_text('include llasm.llinc\n'+''.join(f'include {n}\n' for n in ['tribes_mmx.llinc','tribes_sse.llinc','tribes_scan.llinc','tribes_x87.llinc','tribes_shift.llinc','tribes_atomic.llinc','extern.llinc','code.llinc']))
 tool=R/'generated/tools/llasm/llasm';assert sha(tool)==json.loads((R/'toolchains.lock.json').read_text())['artifacts']['llasm/llasm']
 with (path/'llasm.log').open('w') as log:subprocess.run([str(tool),'-m64','-ptrofs','-O2','-I',str(path),'-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(path/'raw.ll'),str(path/'module.llasm')],cwd=path,stdout=log,stderr=log,check=True)
 procedures,_=va.postprocess(path/'raw.ll',path/'module.ll');assert set(procedures)==added
 module=path/'module.ll';assert 'call void @halopad_trap_unimplemented' not in module.read_text()
 report=dict(discovered=sorted(discovered),reused=sorted(reused),existing_owner_metadata_sha256=sha(pending/'result.json'),added=sorted(added),external=sorted(external),existing_blocks_changed=0,stats=stats,image_sha256=sha(image),srw_sha256=sha(srw),module_sha256=sha(module),selected_source_sha256=hashlib.sha256(selected.encode()).hexdigest(),script_sha256=sha(pathlib.Path(__file__).resolve()))
 (path/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 return [module],report
