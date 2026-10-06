#!/usr/bin/env python3
"""Replay captured dimension stores with the actual intervening protection result."""
import hashlib,json,pathlib,struct
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/display-immediates-candidate';cap=out/'execution';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();d=json.loads((cap/'execution.json').read_text());assert set(d['endpoints'])=={'before','after'} and len(d['services'])==1
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);pages={}
for row in d['endpoints']['before']['ranges']:
 p=cap/row['file'];assert sha(p)==row['sha256'];data=p.read_bytes();u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data);pages[row['address']]=data
states=[]
for name in ['before','after']:
 row=d['endpoints'][name];p=cap/row['cpu_file'];assert sha(p)==row['cpu_sha256'];states.append(p.read_bytes());assert len(states[-1])==440
before,after=states;regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];values=struct.unpack_from('<9I',before);service=values[6]
assert service&~4095 not in pages;u.mem_map(service&~4095,4096);u.mem_write(service,b'\xcc')
for reg,v in zip(regs,values):u.reg_write(reg,v)
pe=pefile.PE(str(R/'generated/srw/run-20261003T190924-ifc22/IFC22.dll'));assert bytes(u.mem_read(0x1005b5ac,0x27))==pe.get_data(0x5b5ac,0x27)
trace=[];writes=[];services=[];assert all(r['args'][4]==1 and r['thread']==1 for r in d['services'])

def code(m,a,n,_):
 if a==service:
  assert len(services)<1;actual=d['services'][len(services)];ret=[0x1005b5c9][len(services)]
  args=list(struct.unpack('<5I',bytes(m.mem_read(m.reg_read(X.UC_X86_REG_ESP),20))));assert args==[ret,*actual['args'][:4]],args
  m.mem_write(args[4],struct.pack('<I',actual['old_protection']));m.reg_write(X.UC_X86_REG_EAX,actual['args'][4]);m.reg_write(X.UC_X86_REG_ESP,m.reg_read(X.UC_X86_REG_ESP)+20);m.reg_write(X.UC_X86_REG_EIP,args[0]);services.append(args);return
 assert 0x1005b5ac<=a<0x1005b5d3,hex(a);trace.append(dict(pc=a,bytes=bytes(m.mem_read(a,n)).hex()))
def write(m,access,a,n,v,_):
 if a<0x1000000 and not (values[4]&~4095)<=a<(values[4]&~4095)+4096:writes.append(dict(pc=m.reg_read(X.UC_X86_REG_EIP),address=a,size=n,value=v))
u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write);u.emu_start(0x1005b5ac,0x1005b5d3,count=64);assert len(services)==1 and u.reg_read(X.UC_X86_REG_EIP)==0x1005b5d3
expected=bytearray(before);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);cpu_diff=[dict(offset=i,native=a,x86=b) for i,(a,b) in enumerate(zip(after,expected)) if a!=b];memory_diff=[]
for row in d['endpoints']['after']['ranges']:
 p=cap/row['file'];assert sha(p)==row['sha256'];data=p.read_bytes();reference=bytes(u.mem_read(row['address'],row['size']));memory_diff += [dict(address=row['address']+i,native=a,x86=b) for i,(a,b) in enumerate(zip(data,reference)) if a!=b]
meta=json.loads((out/'preparation.json').read_text());mode=struct.unpack('<I',bytes(u.mem_read(0x1046d540,4)))[0];width,height=struct.unpack('<ii',bytes(u.mem_read(mode+8,8)));assert writes==[dict(pc=r['writer'],address=r['address'],size=4,value=v) for r,v in zip(meta['records'],[width,height])]
native=R/'generated/srw/run-20261003T190924-ifc22/native-diagnostic/checked/bootstrap-published-ntdll-probe';run=json.loads((native/'result.json').read_text());assert run['binary_sha256']==sha(native/'bootstrap');assert 'Published display immediates after two native-derived stores' in (cap/'display-lldb.log').read_text()
report=dict(passed=not cpu_diff and not memory_diff,cpu_differences=cpu_diff,memory_differences=memory_diff,cpu_bytes=440,guest_bytes_compared=sum(map(len,pages.values())),writes=writes,instructions=trace,services=d['services'],binary_sha256=sha(native/'bootstrap'),sources_sha256={str(p.relative_to(R)):sha(p) for p in [cap/'execution.json',cap/'display-lldb.log',cap/'capture_display.py',cap/'run_capture.py',native/'result.json',out/'preparation.json',out/'test-result.json',pathlib.Path(__file__).resolve()]},scope='Original main through two native-derived dimension stores and the actual intervening successful VirtualProtect call: CPU and five bounded pages compared with x86, replaying the observed API results. No manual thread suspension; other workers can execute between endpoints. No whole-memory, native OS semantic equivalence, complete callback or gameplay acceptance.')
(out/'native-comparison.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS' if report['passed'] else 'FAIL',len(trace),'original instructions;',len(cpu_diff),'CPU and',len(memory_diff),'memory differences');assert report['passed']
