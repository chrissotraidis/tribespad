#!/usr/bin/env python3
"""Compare the actual two-store animation publication with original x86."""
import argparse,json,pathlib
from console_animation_inputs import R,OUT,sha,replay
p=argparse.ArgumentParser();p.add_argument('run',type=pathlib.Path);a=p.parse_args();path=a.run.resolve();run=json.loads(path.read_text())
assert run['initialized'] and run['console_animation_publication'] and run['result']['kind']=='console_animation_native_return'
assert all(run[k] for k in ['checked_image','checked_runtime_objects','checked_support_objects','external_call_gates','independent_subpages_enabled'])
guard_path=path.parent/'guarded/result.json';assert sha(guard_path)==run['external_build_sha256'];guard=json.loads(guard_path.read_text());assert guard['state']=='compiled' and guard['exit']==0
def snapshot(key,prefix):
    paths={}
    for n,r in run[key].items():
        f=R/r['path'];assert sha(f)==r['sha256'] and f.stat().st_size==r['bytes'];paths[n]=f
    table=json.loads(paths[prefix+'-pages.json'].read_text())
    pages=[(r,paths[r['file']].read_bytes()) for r in table['ranges']]
    return paths[prefix+'-cpu.bin'].read_bytes(),table,pages
before,bt,pages=snapshot('console_animation_checkpoint','console-animation')
after,at,final=snapshot('console_animation_final','console-animation-final')
assert bt['resume_pc']==0x10002bc0 and at['resume_pc']==0x10002c23 and len(before)==len(after)==440
trace,expected,memory,permissions=replay(before,pages)
expected_pages={a+o:data[o:o+4096] for a,data in memory.items() for o in range(0,len(data),4096)}
actual_pages={r['address']+o:data[o:o+4096] for r,data in final for o in range(0,len(data),4096)}
actual_permissions={r['address']+o:(r['allocation'],r['protect']) for r,data in final for o in range(0,len(data),4096)}
assert set(expected_pages)==set(actual_pages) and permissions==actual_permissions
errors=[dict(offset=i,native=a,x86=b) for i,(a,b) in enumerate(zip(after,expected)) if a!=b]
memory_errors=[]
for address,reference in expected_pages.items():
    data=actual_pages[address]
    if data!=reference:memory_errors.extend(dict(address=address+i,native=a,x86=b) for i,(a,b) in enumerate(zip(data,reference)) if a!=b)
assert len(run['console_animation_writes'])==len(trace['writes'])==2
for i,(native,w) in enumerate(zip(run['console_animation_writes'],trace['writes']),1):
    assert native==dict(index=i,pc=w['pc'],address=w['address'],size=w['size'],value=int.from_bytes(bytes.fromhex(w['after']),'little'))
report=dict(passed=not errors and not memory_errors,run_path=str(path.relative_to(R)),run_sha256=sha(path),guard_sha256=sha(guard_path),guard_objects=len(guard['objects']),
    preparation_sha256=sha(OUT/'preparation.json'),target_test_sha256=sha(OUT/'test-result.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),reference_helper_sha256=sha(R/'scripts/console_animation_inputs.py'),
    trace=trace,cpu_bytes=440,guest_bytes_compared=sum(map(len,actual_pages.values())),cpu_differences=errors,memory_differences=memory_errors,link_coverage_complete=run['link_coverage_complete'],
    scope='Actual guarded ARM64 endpoints for both animation CALL stores, from the same post-VirtualProtect state as original x86. Full CPU, all readable guest memory, page permissions, two ordered stores and the one intervening protection call compared. No complete initializer, whole target path, renderer or multiplayer claim.')
(OUT/'native-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS' if report['passed'] else 'FAIL',len(trace['instructions']),'instructions, two code writes, one protection call,',report['guest_bytes_compared'],'guest bytes;',len(errors),'CPU differences and',len(memory_errors),'memory differences')
assert report['passed']
