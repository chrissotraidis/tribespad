#!/usr/bin/env python3
"""Compile the pinned pure Bochs math sources through the guest memory pass.

External calls are guarded later against the complete concrete runtime link.
No CPU interpreter or JIT sources are selected.
"""
import concurrent.futures,hashlib,json,os,pathlib,re,subprocess,time
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
lock=next(s for s in json.loads((R/'dependencies.lock.json').read_text())['sources'] if s['name']=='bochs-extended80-candidate')
root=R/lock['path'];out=R/'generated/bochs-math/checked';out.mkdir(exist_ok=True);report=out/'result.json'
if report.exists():
 old=json.loads(report.read_text())
 if old.get('state')=='building':
  live=subprocess.run(['ps','-p',str(old['pid']),'-o','command='],capture_output=True,text=True)
  assert live.returncode or 'build-checked-bochs.py' not in live.stdout,'Existing checked math build is live'
assert subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()==lock['revision']
inputs={}
for name,digest in lock['files'].items():
 path=root/name;assert sha(path)==digest,name;inputs[str(path.relative_to(R))]=digest
for path in [R/'port/bochs-support/math_bridge.cc',R/'port/bochs-support/config.h',R/'port/core/bochs_math.h',R/'port/core/x87_status.h']:
 inputs[str(path.relative_to(R))]=sha(path)
sources=[root/name for name in lock['files'] if name.endswith('.cc')]+[R/'port/bochs-support/math_bridge.cc']
assert all('/cpu/softfloat3e/' in str(p) or '/cpu/fpu/' in str(p) or p.name=='math_bridge.cc' for p in sources)
tools=pathlib.Path('/opt/homebrew/Cellar/llvm@20/20.1.8/bin');plugin=R/'generated/tools/guest-memory-pass/GuestMemoryPass.dylib';pm=plugin.with_name('result.json');meta=json.loads(pm.read_text())
assert sha(plugin)==meta['plugin_sha256'] and sha(R/'port/compiler/GuestMemoryPass.cpp')==meta['source_sha256']
assert all(sha(pathlib.Path(p))==h for p,h in meta['tools_sha256'].items())
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
flags=['-target','arm64-apple-macos12','-isysroot',sdk,'-std=c++17','-O2','-I',str(R/'port/bochs-support'),'-I',str(root/'bochs/cpu/softfloat3e/include'),'-I',str(root/'bochs/cpu'),'-I',str(root/'bochs/cpu/fpu')]
state=dict(state='building',pid=os.getpid(),revision=lock['revision'],sources_sha256=inputs,plugin_build_sha256=sha(pm),script_sha256=sha(pathlib.Path(__file__).resolve()),cpu_interpreter_compiled=False,objects=[],flags=flags)
report.write_text(json.dumps(state,indent=2)+'\n');start=time.monotonic()
def compile_one(source):
 name=source.parent.name+'-'+source.stem;raw=out/(name+'.frontend.ll');ir=out/(name+'.ll');bc=out/(name+'.bc');obj=out/(name+'.o');hints=[]
 commands=[([str(tools/'clang++'),*flags,'-S','-emit-llvm',str(source),'-o',str(raw)],'frontend'),([str(tools/'opt'),'-load-pass-plugin',str(plugin),'-passes=tribes-guest-memory,verify',str(ir),'-o',str(bc)],'pass'),([str(tools/'clang++'),'-target','arm64-apple-macos12','-O0','-Wno-override-module','-c',str(bc),'-o',str(obj)],'codegen')]
 for command,stage in commands:
  log=out/(name+'-'+stage+'.log')
  with log.open('w') as f:run=subprocess.run(command,stdout=f,stderr=f)
  if run.returncode:return dict(source=str(source.relative_to(R)),failed_stage=stage,exit=run.returncode,log=str(log.relative_to(R)))
  if stage=='frontend':
   # Discard only unused optimization hints on internal constant-initialization
   # globals. Keep every initializing store and SSA slot; no memory operation
   # is removed. Unknown forms and used invariant tokens remain hard errors.
   original=raw.read_text();lines=original.splitlines(keepends=True);body=None
   for i,line in enumerate(lines):
    if line.startswith('define '):body=i
    if '@llvm.invariant.start' in line and not line.startswith('declare '):
     match=re.fullmatch(r'  (%[0-9]+) = tail call ptr @llvm\.invariant\.start\.p0\(i64 (?:16|64|96), ptr nonnull (@[_A-Za-z0-9.]+)\)\n',line)
     assert match and body is not None and lines[body].startswith('define internal void @_GLOBAL__sub_I_'),line
     slot,glob=match.groups();end=next(j for j in range(i+1,len(lines)) if lines[j].startswith('}'))
     assert len(re.findall(re.escape(slot)+r'(?![A-Za-z0-9_.$])',''.join(lines[body:end])))==1,'Used invariant token'
     assert re.search(r'^'+re.escape(glob)+r' = internal global .*zeroinitializer, align (?:8|16)$',original,re.M),'Nonlocal invariant'
     hints.append(dict(function=lines[body].strip(),global_name=glob,original_instruction=line.strip()))
     lines[i]=f'  {slot} = freeze ptr null ; unused invariant hint removed before checking\n'
    if line.startswith('}'):body=None
   ir.write_text(''.join(lines))
 census=json.loads(next(l.split(' ',1)[1] for l in (out/(name+'-pass.log')).read_text().splitlines() if l.startswith('TRIBES_MEMORY_PASS ')))
 return dict(source=str(source.relative_to(R)),source_sha256=sha(source),frontend_llvm_sha256=sha(raw),discarded_invariant_hints=hints,llvm_sha256=sha(ir),object=str(obj),object_sha256=sha(obj),bitcode=str(bc),bitcode_sha256=sha(bc),census=census)
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 for result in pool.map(compile_one,sources):
  state['objects'].append(result)
  if len(state['objects'])%25==0:report.write_text(json.dumps(state,indent=2)+'\n');print('Checked math objects:',len(state['objects']),'/',len(sources),flush=True)
errors=[r for r in state['objects'] if 'failed_stage' in r]
state.update(state='failed' if errors else 'compiled',errors=errors,seconds=time.monotonic()-start,scope='Pinned pure arithmetic/transcendental math and bridge compiled with checked memory accesses. External-call coverage requires the final merged link pass; no interpreter/JIT, ABI exemption or startup claim.')
report.write_text(json.dumps(state,indent=2)+'\n');print(state['state'],len(state['objects']),'objects;',len(errors),'failures');raise SystemExit(bool(errors))
