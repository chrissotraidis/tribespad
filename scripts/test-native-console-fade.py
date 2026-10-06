#!/usr/bin/env python3
"""Compare the actual first console CALL publication with original x86."""
import argparse, json, pathlib, struct
import unicorn
from unicorn import x86_const as X
from console_fade_inputs import R, OUT, sha

p = argparse.ArgumentParser(); p.add_argument('run',type=pathlib.Path); a = p.parse_args()
path = a.run.resolve(); run = json.loads(path.read_text())
assert run['initialized'] and run['console_fade_publication'] and run['result']['kind']=='console_fade_native_return'
assert all(run[k] for k in ['checked_image','checked_runtime_objects','checked_support_objects','external_call_gates','independent_subpages_enabled'])
guard_path = path.parent/'guarded/result.json'
assert sha(guard_path)==run['external_build_sha256']
guard = json.loads(guard_path.read_text()); assert guard['state']=='compiled' and guard['exit']==0
def snapshot(key,prefix):
    paths = {}
    for name,row in run[key].items():
        f = R/row['path']; assert sha(f)==row['sha256'] and f.stat().st_size==row['bytes']; paths[name]=f
    table = json.loads(paths[prefix+'-pages.json'].read_text())
    memory = {}; protections = {}
    for row in table['ranges']:
        data = paths[row['file']].read_bytes(); assert len(data)==row['size']
        for off in range(0,len(data),4096):
            address = row['address']+off
            assert address not in memory
            memory[address]=data[off:off+4096]
            protections[address] = (row['allocation'],row['protect'])
    return paths[prefix+'-cpu.bin'].read_bytes(),table,memory,protections
before,bt,initial,bp = snapshot('console_patch_checkpoint','console-patch')
after,at,final,ap = snapshot('console_fade_final','console-fade-final')
assert len(before)==len(after)==440 and bt['resume_pc']==0x1000292d and at['resume_pc']==0x10002938
assert bp==ap and set(initial)==set(final)
u = unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
for address,data in initial.items(): u.mem_map(address,4096); u.mem_write(address,data)
registers=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for reg,value in zip(registers,struct.unpack_from('<9I',before)): u.reg_write(reg,value)
trace=[]; writes=[]
u.hook_add(unicorn.UC_HOOK_CODE,lambda u,pc,n,user:trace.append(dict(pc=pc,bytes=bytes(u.mem_read(pc,n)).hex())))
u.hook_add(unicorn.UC_HOOK_MEM_WRITE,lambda u,access,address,size,value,user:writes.append(dict(pc=u.reg_read(X.UC_X86_REG_EIP),address=address,size=size,value=value)))
u.emu_start(bt['resume_pc'],at['resume_pc'],count=20)
assert len(trace)==4 and u.reg_read(X.UC_X86_REG_EIP)==at['resume_pc']
assert writes==[dict(pc=0x10002936,address=0x5d2860,size=4,value=0x0fa2fcec)]
expected=bytearray(before)
struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in registers])
errors=[dict(offset=i,native=a,x86=b) for i,(a,b) in enumerate(zip(after,expected)) if a!=b]
memory_errors=[]
for address,data in final.items():
    reference=bytes(u.mem_read(address,4096))
    if data!=reference: memory_errors.extend(dict(address=address+i,native=a,x86=b) for i,(a,b) in enumerate(zip(data,reference)) if a!=b)
report=dict(passed=not errors and not memory_errors,run_path=str(path.relative_to(R)),run_sha256=sha(path),
    script_sha256=sha(pathlib.Path(__file__).resolve()),guard_sha256=sha(guard_path),guard_objects=len(guard['objects']),
    preparation_sha256=sha(OUT/'preparation.json'),target_test_sha256=sha(OUT/'test-result.json'),
    instructions=trace,writes=writes,cpu_bytes=440,guest_bytes_compared=sum(map(len,final.values())),cpu_differences=errors,memory_differences=memory_errors,
    link_coverage_complete=run['link_coverage_complete'],
    scope='Actual guarded ARM64 endpoints for the four original instructions after VirtualProtect through the first console CALL store. Full CPU, all readable guest memory and page permissions agree with original x86 from the same state. One native finite revision only; no complete console initializer, render output or multiplayer claim.')
(OUT/'native-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS' if report['passed'] else 'FAIL', 'native console CALL publication:',len(trace),'instructions,',report['guest_bytes_compared'],'guest bytes,',len(errors),'CPU differences,',len(memory_errors),'memory differences')
assert report['passed']
