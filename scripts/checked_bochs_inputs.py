"""Verify and supply checked pure math objects to a concrete external-pass link."""
import hashlib,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1]
def checked_bochs_inputs(platform="macos"):
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();out=R/'generated/bochs-math'/('checked' if platform=='macos' else 'checked-'+platform);path=out/'result.json';meta=json.loads(path.read_text())
 assert meta['state']=='compiled' and not meta['errors'] and not meta['cpu_interpreter_compiled']
 assert sha(R/'scripts/build-checked-bochs.py')==meta['script_sha256']
 assert sha(R/'generated/tools/guest-memory-pass/result.json')==meta['plugin_build_sha256']
 assert all(sha(R/n)==h for n,h in meta['sources_sha256'].items())
 entries=[]
 for row in meta['objects']:
  assert sha(pathlib.Path(row['object']))==row['object_sha256'] and sha(pathlib.Path(row['bitcode']))==row['bitcode_sha256']
  entries.append({k:row[k] for k in ['object','object_sha256','bitcode','bitcode_sha256']})
 return entries,sha(path)
