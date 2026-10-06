#!/usr/bin/env python3
"""Maintainer tool: freeze reviewed edits in the separate Tribes compiler tree."""
import difflib,hashlib,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1];base=R/'generated/tools/SRW';dst=R/'generated/tools/SRW-tribes';p=R/'toolchains.lock.json';lock=json.loads(p.read_text());sources=lock['tribes_extensions']['base_sources'];patch=''
for name,h in sources.items():
 original=base/name;assert hashlib.sha256(original.read_bytes()).hexdigest()==h,'base identity mismatch: '+name
 if original.read_bytes()!=(dst/name).read_bytes():
  patch+=''.join(difflib.unified_diff(original.read_text().splitlines(True),(dst/name).read_text().splitlines(True),fromfile='a/'+name,tofile='b/'+name))
(R/'scripts/patches/srw-tribes.patch').write_text(patch);lock['tribes_extensions']['patch_sha256']=hashlib.sha256(patch.encode()).hexdigest();p.write_text(json.dumps(lock,indent=2)+'\n');print('Frozen source patch:',lock['tribes_extensions']['patch_sha256'])
