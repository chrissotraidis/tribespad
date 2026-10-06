#!/usr/bin/env python3
"""Replay the original eight NOP stores from captured native starting state."""
import hashlib,json,pathlib,struct
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/post-video-nop-second-candidate';cap=out/'execution';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((cap/'execution.json').read_text());assert set(d['endpoints'])=={'before','after'} and not d['services']
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);pages={}
for row in d['endpoints']['before']['ranges']:
 p=cap/row['file'];assert sha(p)==row['sha256'];data=p.read_bytes();u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data);pages[row['address']]=data
states=[]
for name in ['before','after']:
 row=d['endpoints'][name];p=cap/row['cpu_file'];assert sha(p)==row['cpu_sha256'];states.append(p.read_bytes());assert len(states[-1])==440
before,after=states;regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];values=struct.unpack_from('<9I',before)
for reg,v in zip(regs,values):u.reg_write(reg,v)
pe=pefile.PE(str(R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll'));assert bytes(u.mem_read(0x1005b429,0x22))==pe.get_data(0x5b429,0x22)
trace=[];writes=[]
def code(m,a,n,_):
 assert 0x1005b429<=a<0x1005b44b,hex(a);trace.append(dict(pc=a,bytes=bytes(m.mem_read(a,n)).hex()))
def write(m,access,a,n,v,_):
 writes.append(dict(pc=m.reg_read(X.UC_X86_REG_EIP),address=a,size=n,value=v&((1<<(8*n))-1)))
u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write);u.emu_start(0x1005b429,0x1005b44b,count=32);assert u.reg_read(X.UC_X86_REG_EIP)==0x1005b44b and len(trace)==10
expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);cpu_diff=[dict(offset=i,native=a,x86=b) for i,(a,b) in enumerate(zip(after,expected)) if a!=b];memory_diff=[]
for row in d['endpoints']['after']['ranges']:
 p=cap/row['file'];assert sha(p)==row['sha256'];data=p.read_bytes();reference=bytes(u.mem_read(row['address'],row['size']));memory_diff += [dict(address=row['address']+i,native=a,x86=b) for i,(a,b) in enumerate(zip(data,reference)) if a!=b]
meta=json.loads((out/'preparation.json').read_text());assert writes==[dict(pc=w['pc'],address=w['address'],size=w['size'],value=w['value']&((1<<(8*w['size']))-1)) for w in meta['writes']]
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text());assert run['binary_sha256']==sha(native/'bootstrap');assert 'Published second post-video NOP region after eight exact stores' in (cap/'nop-lldb.log').read_text()
report=dict(passed=not cpu_diff and not memory_diff,cpu_differences=cpu_diff,memory_differences=memory_diff,cpu_bytes=440,guest_bytes_compared=sum(map(len,pages.values())),writes=writes,instructions=trace,services=d['services'],binary_sha256=sha(native/'bootstrap'),sources_sha256={str(p.relative_to(R)):sha(p) for p in [cap/'execution.json',cap/'nop-lldb.log',cap/'capture_nop.py',cap/'run_capture.py',native/'result.json',out/'preparation.json',out/'test-result.json',pathlib.Path(__file__).resolve()]},scope='Original main through eight admitted stores: all440 CPU bytes and three bounded pages compared with original x86. No substituted service or manually suspended thread. Other workers can execute between endpoints; no whole-memory or ordinary-timing equivalence, complete callback or gameplay acceptance.')
(out/'native-comparison.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS' if report['passed'] else 'FAIL',len(trace),'original instructions;',len(cpu_diff),'CPU and',len(memory_diff),'memory differences');assert report['passed']
