#!/usr/bin/env python3
"""Archive a terminal diagnostic capture together with immutable code snapshots."""
import argparse,hashlib,json,pathlib,shutil
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('name',choices=['executable-capture-no-sse2','executable-capture-sse2']);a=ap.parse_args();out=R/'generated/qol-initialization';result=json.loads((out/'result.json').read_text());assert result['state']=='finished' and result['executable_reference']['enabled'];assert result['cpu_profile']['sse2']==(a.name=='executable-capture-sse2')
destination=out/(a.name+'-snapshots');destination.mkdir(exist_ok=True)
for snapshot in result['executable_reference']['snapshots']:
 source=R/snapshot['path'];assert source.stat().st_size==snapshot['bytes'] and hashlib.sha256(source.read_bytes()).hexdigest()==snapshot['sha256'];target=destination/source.name;shutil.copyfile(source,target);snapshot['path']=str(target.relative_to(R))
result['archive_driver_sha256']=hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest();(out/(a.name+'.json')).write_text(json.dumps(result,indent=2)+'\n');print('Archived terminal capture and validated snapshots:',a.name)
