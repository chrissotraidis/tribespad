#!/usr/bin/env python3
"""Record a whole-image diagnostic ARM64 object, including orphan recovery."""
import argparse, datetime, hashlib, json, pathlib, struct, subprocess
p=argparse.ArgumentParser();p.add_argument('lift',type=pathlib.Path);a=p.parse_args()
out=a.lift.resolve()/'native-census';obj=out/'engine.o'
recovery=out/'recovery.json'
if recovery.exists():
    recovered=json.loads(recovery.read_text())
    proc=subprocess.run(['ps','-p',str(recovered['compiler_child_pid']),'-o','command='],capture_output=True,text=True)
    if proc.returncode==0 and str(out/'engine.ll') in proc.stdout:
        raise SystemExit('Compiler still active; its output is not final.')
with obj.open('rb') as f:
    magic,cpu,subtype,filetype=struct.unpack('<IIII',f.read(16))
assert (magic,cpu,filetype)==(0xfeedfacf,0x100000c,1),'Expected ARM64 Mach-O object'
defined=subprocess.check_output(['nm','--defined-only',str(obj)],text=True)
undefined=subprocess.check_output(['nm','-u',str(obj)],text=True)
(out/'defined-symbols.txt').write_text(defined)
(out/'undefined-symbols.txt').write_text(undefined)
report=json.loads((out/'result.json').read_text())
report['object']={'verified':True,'sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),'bytes':obj.stat().st_size,'identity':subprocess.check_output(['file',str(obj)],text=True).strip(),'defined_text_symbols':sum(' T ' in line or ' t ' in line for line in defined.splitlines()),'unresolved_symbols':len(undefined.splitlines()),'inspection_time':datetime.datetime.now(datetime.timezone.utc).isoformat()}
report['playable']=False
if recovery.exists():
    report['stages']['clang']['state']='terminal_object_verified'
    if 'exit' in report['stages']['clang']:
        report['stages']['clang']['launcher_exit']=report['stages']['clang'].pop('exit')
    report['stages']['clang']['compiler_child_exit']='unavailable (orphan; object verified independently)'
    recovered['state']='terminal_object_verified';recovered['object']=report['object']
    recovery.write_text(json.dumps(recovered,indent=2)+'\n')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['object'],indent=2))
