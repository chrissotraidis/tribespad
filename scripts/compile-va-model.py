#!/usr/bin/env python3
"""Compile the prepared original-address engine without pretending to link a game."""
import argparse,hashlib,json,pathlib,subprocess,time
p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);p.add_argument('--platform',choices=['macos','device','simulator'],default='macos');a=p.parse_args();va=a.lift.resolve()/'va';out=va if a.platform=='macos' else va/a.platform;out.mkdir(parents=True,exist_ok=True);meta=json.loads((va/'result.json').read_text());assert meta['whole_game_entered'] is False
report_path=out/'compile.json'
if report_path.exists():
 old=json.loads(report_path.read_text())
 if old.get('state')=='running':
  probe=subprocess.run(['ps','-p',str(old['pid']),'-o','command='],capture_output=True,text=True)
  if probe.returncode==0 and str(va/'engine.ll') in probe.stdout:raise SystemExit('Existing compiler is live: '+str(old['pid']))
input_hash=hashlib.sha256((va/'engine.ll').read_bytes()).hexdigest()
assert meta['prepared'] and meta['engine_ll_sha256']==input_hash, 'Address preparation is incomplete or changed'
if report_path.exists() and old.get('exit')==0 and old.get('input_sha256')==input_hash and (out/'engine.o').exists():raise SystemExit('Verified current compilation already exists; inspect compile.json.')
target={'macos':'arm64-apple-macos12','device':'arm64-apple-ios16','simulator':'arm64-apple-ios16-simulator'}[a.platform];sdk={'macos':'macosx','device':'iphoneos','simulator':'iphonesimulator'}[a.platform];sdkpath=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
report={'platform':a.platform,'target':target,'sdk':sdkpath,'playable':False,'input_sha256':input_hash,'support_sha256':meta['support_sha256'],'state':'running'}
start=time.monotonic()
with (out/'clang.log').open('w') as log:
 worker=subprocess.Popen(['clang','-target',target,'-isysroot',sdkpath,'-O0','-Wno-override-module','-c',str(va/'engine.ll'),'-o',str(out/'engine.o')],stdout=log,stderr=log)
 report['pid']=worker.pid;report_path.write_text(json.dumps(report,indent=2)+'\n')
 while True:
  try:code=worker.wait(timeout=30);break
  except subprocess.TimeoutExpired:
   report['seconds']=time.monotonic()-start;report_path.write_text(json.dumps(report,indent=2)+'\n');print('Same ARM64 compiler running: PID '+str(worker.pid),flush=True)
report.update(state='finished',exit=code,seconds=time.monotonic()-start)
if code==0:
 report['object_sha256']=hashlib.sha256((out/'engine.o').read_bytes()).hexdigest();report['object_bytes']=(out/'engine.o').stat().st_size;report['object_identity']=subprocess.check_output(['file',str(out/'engine.o')],text=True).strip()
report_path.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)
