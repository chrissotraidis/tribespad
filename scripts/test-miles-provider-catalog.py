#!/usr/bin/env python3
"""Reject synthetic import collisions before generating a complete Miles link."""
import copy,hashlib,json,pathlib,tempfile
from codec_runtime_inputs import prepare,validate_catalog
from miles_runtime_inputs import prepare as prepare_miles
R=pathlib.Path(__file__).resolve().parents[1];rows=json.loads((R/'config/runtime/miles-codecs.json').read_text());validate_catalog(rows)
rejected=[]
for bad in [0xfffa0000,0xfffb0000,0xfffbf000,0xfffc0000,0xfffe0000,0xffff0000,0xffe10001,rows[0]['import_base']]:
 candidate=copy.deepcopy(rows);candidate[-1]['import_base']=bad
 try:validate_catalog(candidate)
 except AssertionError:rejected.append(hex(bad))
 else:raise AssertionError(('accepted invalid/colliding namespace',hex(bad)))
with tempfile.TemporaryDirectory() as name:
 out=pathlib.Path(name);_,modules,imports=prepare(out);_,miles,links=prepare_miles(out,imports)
 images=sorted((m['image_base'],m['image_base']+m['image_size']) for m in modules+[miles]);assert all(a[1]<=b[0] for a,b in zip(images,images[1:]))
 assert len({r['guest_va'] for r in imports+miles['imports']})==len(imports+miles['imports'])
 assert all([m['entry'],'loc_%X'%m['entry']] in m['entries'] for m in modules)
 summary=[dict(module=m['config']['module'],entries=len(m['entries']),explicit_stops=len(m['unsupported_instructions']),lift=m['config']['lift']) for m in modules]
report=dict(passed=True,rejected_import_bases=rejected,modules=summary,original_miles_routes=len(links),script_sha256=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),catalog_sha256=hashlib.sha256((R/'config/runtime/miles-codecs.json').read_bytes()).hexdigest(),scope='Finite catalog namespace/identity/entry/range/import checks. Original DLL initialization requires a separate full engine run.')
out=R/'generated/integration-review/m3d';out.mkdir(exist_ok=True);(out/'catalog-check.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',len(modules),'providers;',len(links),'original Miles routes;',len(rejected),'collision/invalid namespaces rejected')
