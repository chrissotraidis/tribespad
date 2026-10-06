#!/usr/bin/env python3
"""Identify the first real SDL hint and retained timer output from native state."""
import hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
out=R/'generated/integration-review/sdl-hint-research';out.mkdir(exist_ok=True)
run=json.loads((native/'result.json').read_text());result=run['result']
assert run['exit']==71 and run['initialized'] and result['kind']=='import'
assert result['import']['name']=='SDL_SetHint' and result['original_iat_call_verified']
assert result['original_import_path']['call_pc']==0x10060604 and result['return_pc']==0x1006060a
capture=run['import_checkpoint']
for row in capture.values():assert sha(R/row['path'])==row['sha256']
table=json.loads((native/'import-pages.json').read_text());cpu=(native/'import-cpu.bin').read_bytes()
assert len(cpu)==440 and table['resume_pc']==result['import']['guest_va']
pages=[(row,(native/row['file']).read_bytes()) for row in table['ranges']]
def read(address,size):
    for row,data in pages:
        if row['address']<=address and address+size<=row['address']+len(data):return data[address-row['address']:address-row['address']+size]
    raise AssertionError((hex(address),size))
def string(address):
    data=bytearray()
    for n in range(4096):
        value=read(address+n,1)[0]
        if not value:return data.decode('utf-8')
        data.append(value)
    raise AssertionError('unterminated captured string')
regs=struct.unpack_from('<9I',cpu);esp,ebp=regs[4:6]
assert esp==result['esp'] and ebp==result['registers']['ebp']
stack=struct.unpack('<3I',read(esp,12));assert stack==(result['return_pc'],*result['stack_arguments'][:2])
name,value=map(string,stack[1:]);assert name=='SDL_TIMER_RESOLUTION' and value=='1'
assert read(0x10060604,6).hex()==result['original_import_path']['call_bytes']=='ff1514941210'
caps=struct.unpack('<2I',read(ebp-0x10,8));assert caps==(1,65535)
assert read(0x1046e40c,1)==b'\x01' and string(0x101ba1fc)=='%d'
report=dict(passed=True,hint=name,value=value,name_pointer=stack[1],value_pointer=stack[2],return_pc=stack[0],timer_caps_address=ebp-0x10,timer_caps=list(caps),selected_period=1,
    capture=capture,inputs_sha256={str(p.relative_to(R)):sha(p) for p in [native/'result.json',native/'import-cpu.bin',native/'import-pages.json',pathlib.Path(__file__).resolve()]},
    scope='Actual SDL_SetHint arguments and retained successful native TIMECAPS output. Complete CPU and readable pages are captured at the original import entry, without returning a service result. Hint priority/storage/allocator/backend integration remains unimplemented.')
(out/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS: original SDL_SetHint("SDL_TIMER_RESOLUTION", "1"); retained native TIMECAPS {1,65535}')
