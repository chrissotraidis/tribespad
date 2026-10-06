#!/usr/bin/env python3
"""Recheck existing filesystem consumers after changing shared path resolution."""
import hashlib,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
out=R/'generated/native-win32/current-directory-regressions';out.mkdir(parents=True,exist_ok=True)
report_path=out/'result.json';report_path.write_text(json.dumps({'state':'running','pass':False})+'\n');rows={}
for name,deps in [('guest_filesystem',['guest_heap']),('guest_file',['guest_heap','guest_filesystem','native_handles','guest_memory_checks']),('guest_find',['guest_heap','guest_filesystem']),('process_environment',['guest_heap','guest_filesystem'])]:
 sources=[R/'port/core'/(n+'.c') for n in [name,*deps]]+[R/'tests'/(name+'_test.c')];runs=[]
 for mode in ['normal','tsan']:
  exe=out/(name+'-'+mode)
  command=['clang','-arch','arm64','-O2','-Wall','-Wextra','-Werror','-pthread']+(['-fsanitize=thread'] if mode=='tsan' else [])+list(map(str,sources))+['-o',str(exe)]
  compilation=subprocess.run(command,capture_output=True,text=True);(out/(name+'-'+mode+'-compile.log')).write_text(compilation.stdout+compilation.stderr);assert not compilation.returncode,compilation.stderr
  run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/(name+'-'+mode+'.log')).write_text(run.stdout+run.stderr)
  assert not run.returncode and run.stdout.startswith('PASS:') and not run.stderr,(name,mode,run.stdout,run.stderr)
  runs.append({'mode':mode,'output':run.stdout.strip(),'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()})
 rows[name]={'runs':runs,'sources_sha256':{str(s.relative_to(R)):hashlib.sha256(s.read_bytes()).hexdigest() for s in sources}}
report={'state':'finished','pass':True,'tests':rows,'test_driver_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'scope':'Existing native filesystem, file-open, directory-search and environment contracts; no game startup/session claim'}
report_path.write_text(json.dumps(report,indent=2)+'\n');print('PASS: filesystem/file/search/environment regression suites, normal and ThreadSanitizer')
