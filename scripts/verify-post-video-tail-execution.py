#!/usr/bin/env python3
"""Replay captured main-thread tail with its two actual observed protection results."""
import hashlib,json,pathlib,struct
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/post-video-tail-candidate';cap=out/'execution';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((cap/'execution.json').read_text());assert set(d['endpoints'])=={'before','after'} and len(d['services'])==2
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);pages={}
for row in d['endpoints']['before']['ranges']:
 p=cap/row['file'];assert sha(p)==row['sha256'];data=p.read_bytes();u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data);pages[row['address']]=data
states=[]
for name in ['before','after']:
 row=d['endpoints'][name];p=cap/row['cpu_file'];assert sha(p)==row['cpu_sha256'];states.append(p.read_bytes());assert len(states[-1])==440
before,after=states;regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];values=struct.unpack_from('<9I',before);service=values[6]
assert service&~4095 not in pages;u.mem_map(service&~4095,4096);u.mem_write(service,b'\xcc')
for reg,v in zip(regs,values):u.reg_write(reg,v)
pe=pefile.PE(str(R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll'));assert bytes(u.mem_read(0x1005b4bb,0x36))==pe.get_data(0x5b4bb,0x36)
trace=[];writes=[];services=[];assert all(r['args'][4]==1 and r['thread']==1 for r in d['services'])

def code(m,a,n,_):
 if a==service:
  assert len(services)<2;actual=d['services'][len(services)];ret=[0x1005b4cc,0x1005b4e6][len(services)]
  args=list(struct.unpack('<5I',bytes(m.mem_read(m.reg_read(X.UC_X86_REG_ESP),20))));assert args==[ret,*actual['args'][:4]],args
  m.mem_write(args[4],struct.pack('<I',actual['old_protection']));m.reg_write(X.UC_X86_REG_EAX,actual['args'][4]);m.reg_write(X.UC_X86_REG_ESP,m.reg_read(X.UC_X86_REG_ESP)+20);m.reg_write(X.UC_X86_REG_EIP,args[0]);services.append(args);return
 assert 0x1005b4bb<=a<0x1005b4f1,hex(a);trace.append(dict(pc=a,bytes=bytes(m.mem_read(a,n)).hex()))
def write(m,access,a,n,v,_):
 if a<0x1000000 and not (values[4]&~4095)<=a<(values[4]&~4095)+4096:writes.append(dict(pc=m.reg_read(X.UC_X86_REG_EIP),address=a,size=n,value=v))
u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write);u.emu_start(0x1005b4bb,0x1005b4f1,count=64);assert len(services)==2 and u.reg_read(X.UC_X86_REG_EIP)==0x1005b4f1
expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);cpu_diff=[dict(offset=i,native=a,x86=b) for i,(a,b) in enumerate(zip(after,expected)) if a!=b];memory_diff=[]
for row in d['endpoints']['after']['ranges']:
 p=cap/row['file'];assert sha(p)==row['sha256'];data=p.read_bytes();reference=bytes(u.mem_read(row['address'],row['size']));memory_diff += [dict(address=row['address']+i,native=a,x86=b) for i,(a,b) in enumerate(zip(data,reference)) if a!=b]
meta=json.loads((out/'preparation.json').read_text());assert writes==[dict(pc=r['write']['pc'],address=r['write']['address'],size=r['write']['size'],value=int.from_bytes(bytes.fromhex(r['write']['after']),'little')) for r in meta['records']]
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text());assert run['binary_sha256']==sha(native/'bootstrap');assert 'Published post-video tail after three exact stores' in (cap/'tail-lldb.log').read_text()
report=dict(passed=not cpu_diff and not memory_diff,cpu_differences=cpu_diff,memory_differences=memory_diff,cpu_bytes=440,guest_bytes_compared=sum(map(len,pages.values())),writes=writes,instructions=trace,services=d['services'],binary_sha256=sha(native/'bootstrap'),sources_sha256={str(p.relative_to(R)):sha(p) for p in [cap/'execution.json',cap/'tail-lldb.log',cap/'capture_tail.py',cap/'run_capture.py',native/'result.json',out/'preparation.json',out/'test-result.json',pathlib.Path(__file__).resolve()]},scope='Original main through three admitted stores and two actual successful VirtualProtect calls: CPU and five bounded pages compared with x86, replaying the observed API results. No manual thread suspension; other workers can execute between endpoints. No whole-memory, native OS semantic equivalence, complete callback or gameplay acceptance.')
(out/'native-comparison.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS' if report['passed'] else 'FAIL',len(trace),'original instructions;',len(cpu_diff),'CPU and',len(memory_diff),'memory differences');assert report['passed']
