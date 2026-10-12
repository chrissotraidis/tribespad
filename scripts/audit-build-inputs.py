#!/usr/bin/env python3
"""Record every file a Python build script opens and every program it starts.

Usage: TB_AUDIT_LOG=/tmp/inputs.jsonl scripts/audit-build-inputs.py scripts/build-ipad-engine.py [args]
Writes one JSON array per event: ["open", path, mode] or ["exec", argv].
"""
import sys,runpy,json,os
log=open(os.environ['TB_AUDIT_LOG'],'w')
R=os.path.realpath(os.path.join(os.path.dirname(__file__),'..'))
def hook(ev,args):
    try:
        if ev=='open':
            p=args[0]
            if isinstance(p,(str,bytes,os.PathLike)): log.write(json.dumps(['open',os.fspath(p) if not isinstance(p,bytes) else p.decode(errors='replace'),str(args[1])])+'\n')
        elif ev=='subprocess.Popen':
            log.write(json.dumps(['exec',[os.fspath(a) if not isinstance(a,bytes) else a.decode(errors='replace') for a in (args[1] if isinstance(args[1],(list,tuple)) else [args[1]])]])+'\n')
        elif ev in ('os.listdir','os.scandir','glob.glob'):
            log.write(json.dumps([ev,str(args[0])])+'\n')
    except Exception: pass
sys.addaudithook(hook)
script=sys.argv[1];sys.argv=sys.argv[1:]
sys.path.insert(0,os.path.dirname(os.path.abspath(script)))
runpy.run_path(script,run_name='__main__')
