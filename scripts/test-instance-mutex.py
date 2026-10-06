#!/usr/bin/env python3
"""Exercise the original instance-detection contract with independent processes."""
import hashlib,importlib.util,json,pathlib,signal,subprocess,tempfile,selectors
from checked_support import compile_support
from guard_checked_link import guard_link
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/integration-review/instance-mutex';out.mkdir(exist_ok=True)
spec=importlib.util.spec_from_file_location('bindings',R/'scripts/gen-win32-bindings.py');gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen);bindings=R/'port/llasm-runtime/win32-bindings.json';wrapper=out/'bindings.ll';wrapper.write_text(gen.generate({name:json.loads(bindings.read_text())[name] for name in ['CreateMutexA','CloseHandle']}))
sources=[R/name for name in ['port/core/guest_heap.c','port/core/native_handles.c','tests/fixtures/instance_mutex_dispatch.c','tests/fixtures/native_enter.ll']]+[wrapper]
objects,checked=compile_support(sources,out/'checked',[]);meta=json.loads((out/'checked/result.json').read_text());entries=[]
for obj in objects:
 p=pathlib.Path(obj);row=meta['objects'][p.stem];entries.append(dict(object=obj,object_sha256=sha(p),bitcode=str(p.with_suffix('.bc')),bitcode_sha256=row['bitcode_sha256']))
objects,guarded=guard_link(entries,out/'guarded');test=R/'tests/instance_mutex_test.c';guards=[R/'port/core/guest_memory_checks.c',R/'port/core/guest_external_checks.c'];report=dict(passed=False,checked=checked,guarded=guarded,runs={});(out/'result.json').write_text(json.dumps(report)+'\n')
for mode in ['checked','normal','tsan']:
 exe=out/('test-'+mode);command=['clang','-target','arm64-apple-macos12','-O1','-g','-DPTROFS_64BIT','-Wall','-Wextra','-Werror','-Wno-override-module',str(test),*map(str,guards)]
 command+=['-DCHECKED_TEST',*objects] if mode=='checked' else list(map(str,sources))
 if mode=='tsan':command+=['-fsanitize=thread']
 subprocess.run([*command,'-o',str(exe)],check=True)
 with tempfile.TemporaryDirectory(prefix='tribes-instance-') as namespace:
  run=subprocess.run([str(exe),namespace,'test'],capture_output=True,text=True,timeout=45);(out/(mode+'.log')).write_text(run.stdout+run.stderr);assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(mode,run)
  alive=[]
  def start():
   p=subprocess.Popen([str(exe),namespace,'hold'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True);alive.append(p);return p
  def ready(p):
   with selectors.DefaultSelector() as s:
    s.register(p.stdout,selectors.EVENT_READ);assert s.select(20),'mutex process did not become ready'
   line=p.stdout.readline();assert line.strip() in ['0','183'],(line,p.poll(),p.stderr.read() if p.poll() is not None else 'running');return int(line)
  def close(p):
   p.stdin.write('\n');p.stdin.flush();stdout,stderr=p.communicate(timeout=20);assert p.returncode==0 and not stdout and not stderr,(p.returncode,stdout,stderr)
  try:
   group=[start() for _ in range(8)];errors=[ready(p) for p in group];assert errors.count(0)==1 and errors.count(183)==7,errors
   creator=group[errors.index(0)];close(creator);extra=start();assert ready(extra)==183
   for p in group:
    if p is not creator:close(p)
   close(extra);creator=start();assert ready(creator)==0
   observer=start();assert ready(observer)==183;creator.kill();creator.communicate(timeout=20)
   extra=start();assert ready(extra)==183;close(observer);close(extra)
   creator=start();assert ready(creator)==0;creator.kill();creator.communicate(timeout=20)
   extra=start();assert ready(extra)==0;close(extra)
  finally:
   for p in alive:
    if p.poll() is None:p.kill();p.communicate()
  if mode!='tsan':
   for reject in ['name','security','unowned','wait','duplicate']+(['memory'] if mode=='checked' else []):
    run=subprocess.run([str(exe),namespace,'reject-'+reject],capture_output=True,text=True,timeout=20);assert run.returncode==-signal.SIGABRT,(reject,run)
    (out/(mode+'-reject-'+reject+'.log')).write_text(run.stdout+run.stderr)
 report['runs'][mode]=dict(passed=True,binary_sha256=sha(exe),concurrent_processes=8)
report.update(passed=True,sources_sha256={str(p.relative_to(R)):sha(p) for p in [*sources,test,*guards,bindings,R/'port/core/native_handles.h',R/'config/runtime/external-memory-contracts.json',pathlib.Path(__file__).resolve()]},scope='Exact Tribes2Game initial-owner CreateMutexA/CloseHandle path in one configured virtual session namespace; final-handle lifetime across independent processes and process death. General wait/release, alternate names, security, inheritance and duplication remain explicit gates.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS: named instance mutex normal, checked and ThreadSanitizer; two emitted ABIs, concurrent processes, surviving openers and crash cleanup')
