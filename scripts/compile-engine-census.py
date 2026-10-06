#!/usr/bin/env python3
"""Compile whole-image diagnostic LLVM. Trapped code is never a playable artifact."""
import argparse,hashlib,json,pathlib,re,subprocess,time
import pefile
R=pathlib.Path(__file__).resolve().parents[1];p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);p.add_argument('--observe-seconds',type=int,default=30);a=p.parse_args();lift=a.lift.resolve();result=json.loads((lift/'result.json').read_text());assert result['exit']==0 and result['full_flags'],'Requires successful conservative translation'
assert result['image_sha256']==hashlib.sha256((lift/'Tribes2.exe').read_bytes()).hexdigest();tool=R/'generated/tools/llasm/llasm';assert hashlib.sha256(tool.read_bytes()).hexdigest()==json.loads((R/'toolchains.lock.json').read_text())['artifacts']['llasm/llasm']
out=lift/'native-census';out.mkdir(exist_ok=True)
existing_report=out/'result.json'
if existing_report.exists():
 previous=json.loads(existing_report.read_text())
 for stage,info in previous.get('stages',{}).items():
  if info.get('state')=='running' and info.get('pid'):
   check=subprocess.run(['ps','-p',str(info['pid']),'-o','command='],capture_output=True,text=True)
   if check.returncode==0 and (str(out/'engine.ll') in check.stdout or str(lift/'tribes2.llasm') in check.stdout):raise SystemExit('Existing '+stage+' worker is live: PID '+str(info['pid']))
recovery=out/'recovery.json'
if recovery.exists():
 active=json.loads(recovery.read_text());check=subprocess.run(['ps','-p',str(active['compiler_child_pid']),'-o','command='],capture_output=True,text=True)
 if check.returncode==0 and str(out/'engine.ll') in check.stdout:raise SystemExit('Existing compiler is live; inspect PID '+str(active['compiler_child_pid'])+' before continuing.')
report={'playable':False,'translation_diagnostic':result['diagnostic'],'lift':str(lift),'image_sha256':result['image_sha256'],'srw_sha256':result['srw_sha256'],'stages':{}}
report['support_sha256']={str(path.relative_to(R)):hashlib.sha256(path.read_bytes()).hexdigest() for directory in [R/'port/llasm-support',R/'ref/halopad/port/llasm-support'] for path in directory.glob('*.llinc')}
def run(stage,command):
 start=time.monotonic()
 with (out/(stage+'.log')).open('w') as log:
  process=subprocess.Popen(command,stdout=log,stderr=log)
  report['stages'][stage]={'state':'running','pid':process.pid}
  (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
  while True:
   try:code=process.wait(timeout=max(1,a.observe_seconds));break
   except subprocess.TimeoutExpired:
    # Observation expiry is not process completion. Keep the same compiler.
    report['stages'][stage]['seconds']=time.monotonic()-start
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(stage+' still running: PID '+str(process.pid),flush=True)
 report['stages'][stage]={'exit':code,'seconds':time.monotonic()-start}
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');return code==0
if not run('llasm',[str(tool),'-m64','-ptrofs','-O2','-I',str(R/'port/llasm-support'),'-I',str(R/'ref/halopad/port/llasm-support'),'-o',str(out/'raw.ll'),str(lift/'tribes2.llasm')]):raise SystemExit('LLASM failed: '+str(out/'llasm.log'))
# MSVC export spellings are legal linker symbols, but require quoted LLVM IDs.
# Preserve each original name; this is syntax quoting, not an import stub.
pe=pefile.PE(str(lift/'Tribes2.exe'));names={imp.name.decode() for dll in pe.DIRECTORY_ENTRY_IMPORT for imp in dll.imports if imp.name};special=sorted((n for n in names if not re.fullmatch(r'[-a-zA-Z$._][-a-zA-Z$._0-9]*',n)),key=len,reverse=True)
quoted={n:'@"'+''.join(chr(b) if 32<=b<127 and b not in [34,92] else '\\'+format(b,'02X') for b in n.encode())+'"' for n in special}
pattern=re.compile('@('+ '|'.join(re.escape(n) for n in special)+r')(?![\w$?.@])') if special else None
with (out/'raw.ll').open() as src,(out/'engine.ll').open('w') as dst:
 for line in src:dst.write(pattern.sub(lambda m:quoted[m[1]],line) if pattern else line)
report['quoted_imports']=special;(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
if not run('clang',['clang','-arch','arm64','-O0','-Wno-override-module','-c',str(out/'engine.ll'),'-o',str(out/'engine.o')]):raise SystemExit('LLVM/ARM64 compilation failed: '+str(out/'clang.log'))
report['object_sha256']=hashlib.sha256((out/'engine.o').read_bytes()).hexdigest();report['object_identity']=subprocess.check_output(['file',str(out/'engine.o')],text=True).strip();(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
