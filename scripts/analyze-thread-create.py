#!/usr/bin/env python3
"""Identify the first actual guest thread request, without creating/faking it."""
import hashlib,json,pathlib,struct
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=R/'generated/srw/run-20261003T101208-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe'
run=json.loads((native/'result.json').read_text());r=run['result'];assert run['exit']==71 and run['initialized'] and r['engine_entered'] and r['import']['name']=='CreateThread' and r['original_iat_call_verified']
assert r['original_import_path']['call_pc']==0x1010de04 and r['return_pc']==0x1010de0a
for row in run['import_checkpoint'].values():assert sha(R/row['path'])==row['sha256']
cpu=(native/'import-cpu.bin').read_bytes();pages=json.loads((native/'import-pages.json').read_text());assert len(cpu)==440 and pages['resume_pc']==r['import']['guest_va']
ranges=[(row,(native/row['file']).read_bytes()) for row in pages['ranges']]
def read(address,size):
 for row,data in ranges:
  if row['address']<=address and address+size<=row['address']+len(data):return data[address-row['address']:address-row['address']+size]
 raise AssertionError((hex(address),size))
esp=struct.unpack_from('<I',cpu,16)[0];assert esp==r['esp']
args=list(struct.unpack('<7I',read(esp,28)));assert args==[r['return_pc']]+r['stack_arguments'][:6]
_,security,stack_size,start,context,flags,tid_out=args
assert security==0 and stack_size==0x800000 and start==0x1010dc5f and flags==4
words=list(struct.unpack('<5I',read(context,20)));assert words[0]==0x100759a0 and words[3]==0x10000000
assert read(0x1010de04,6).hex()==r['original_import_path']['call_bytes']=='ff15e8921210'
report=dict(passed=True,security_attributes=security,stack_size=stack_size,crt_start=start,crt_context=context,creation_flags=flags,thread_id_output=tid_out,crt_context_words=words,guest_start=words[0],guest_parameter=words[1],retained_module=words[3],guest_parameter_words=list(struct.unpack('<3I',read(words[1],12))),
 inputs_sha256={str(p.relative_to(R)):sha(p) for p in [native/'result.json',native/'import-cpu.bin',native/'import-pages.json',pathlib.Path(__file__).resolve()]},capture=run['import_checkpoint'],scope='Actual original CreateThread import entry and retained CRT module handle, without returning a thread handle or executing guest thread code. Native CreateThread/Resume/Exit, TEB/static TLS and original DLL thread notifications still required.')
out=R/'generated/integration-review/module-ex/thread-request.json';out.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ['capture','inputs_sha256']},indent=2))
