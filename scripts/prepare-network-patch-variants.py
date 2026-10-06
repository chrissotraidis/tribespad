#!/usr/bin/env python3
"""Extract complete strict network-patched definitions, without activation."""
import hashlib,json,pathlib,re,subprocess
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=R/'generated/integration-review/followup-prefix-engine/va/engine.ll';new=R/'generated/integration-review/network-patched-engine/va/engine.ll';out=new.parent.parent/'variants';out.mkdir(exist_ok=True)
meta=json.loads(new.with_name('result.json').read_text());assert sha(new)==meta['engine_ll_sha256'];parent=json.loads((new.parent.parent/'result.json').read_text());assert parent['trace_sha256']==sha(R/parent['trace_path']) and parent['complete_phase']
old_compile=json.loads(old.with_name('result.json').read_text());assert sha(old)==old_compile['engine_ll_sha256']
def functions(path):
 name=None;lines=[]
 with path.open() as f:
  for line in f:
   if line.startswith('define '):
    assert name is None;match=re.search(r'@([A-Za-z_0-9]+)\(',line);assert match;name=match[1];lines=[]
   if name:
    lines.append(line)
    if line.strip()=='}':yield name,''.join(lines);name=None
 assert name is None
# Exposing a continuation can change hidden/protected linkage alone. Exclude
# those unchanged bodies; retain actual instructions, declarations and CFG.
def semantic_hash(body):return hashlib.sha256(re.sub(r'^define protected ', 'define hidden ',body).encode()).hexdigest()
old_hashes={name:semantic_hash(body) for name,body in functions(old)}
new_hashes={};selected={}
for name,body in functions(new):
 h=hashlib.sha256(body.encode()).hexdigest();new_hashes[name]=h
 if old_hashes.get(name)!=semantic_hash(body):selected[name]=body
removed=sorted(set(old_hashes)-set(new_hashes));assert not removed,('Old entry labels removed from later variant',removed)
assert selected and all(re.fullmatch('loc_[0-9A-F]+',n) for n in selected)
preamble=[]
with new.open() as f:
 for line in f:
  if line.startswith('define '):break
  preamble.append(line)
body='\n'.join(selected[n] for n in sorted(selected));refs=set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',body))-set(selected)
declared=set(re.findall(r'@([A-Za-z_][A-Za-z_0-9]*)\(',''.join(preamble)));missing=refs-declared
assert all(re.fullmatch('loc_[0-9A-F]+',n) for n in missing),missing
module=out/'module.ll';module.write_text(''.join(preamble)+''.join('declare fastcc void @%s(%%_cpu*)\n'%n for n in sorted(missing))+body)
obj=out/'module.o';subprocess.run(['/opt/homebrew/Cellar/llvm@20/20.1.8/bin/clang','-target','arm64-apple-macos12','-O0','-Wno-override-module','-c',str(module),'-o',str(obj)],check=True)
report=dict(state='compiled_unpublished',old_llvm_sha256=sha(old),new_llvm_sha256=sha(new),preparation_sha256=sha(new.parent.parent/'result.json'),trace_sha256=parent['trace_sha256'],complete_phase=True,published=False,module_sha256=sha(module),object_sha256=sha(obj),changed_existing=sorted(set(selected)&set(old_hashes)),added=sorted(set(selected)-set(old_hashes)),removed=removed,external_targets=sorted(refs),function_sha256={n:new_hashes[n] for n in selected},script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Complete changed definitions versus preceding verified followup engine. Completed reference suffix; no runtime switching or behavioral equivalence claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('Compiled unpublished prefix:',len(selected),'definitions,',len(report['added']),'added,',len(refs),'external references')
