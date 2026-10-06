#!/usr/bin/env python3
"""Compare the actual checked ARM64 library patch routine with original x86."""
import argparse,hashlib,json,pathlib,struct,subprocess,sys
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('run',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path,default=R/'generated/integration-review/native-library-patch-test');a=p.parse_args();source=a.run.resolve();run=json.loads(source.read_text());out=a.output.resolve();out.mkdir(exist_ok=True)
assert run['initialized'] and run['library_patch_publication'] and run['result']['kind']=='library_patch_native_return'
def snapshots(key,prefix):
 paths={}
 for name,row in run[key].items():
  path=R/row['path'];assert sha(path)==row['sha256'] and path.stat().st_size==row['bytes'];paths[name]=path
 manifest=json.loads(paths[prefix+'-pages.json'].read_text());memory={};protections={}
 for row in manifest['ranges']:
  data=paths[row['file']].read_bytes();assert len(data)==row['size']
  for off in range(0,len(data),4096):
   address=row['address']+off;assert address not in memory
   memory[address]=data[off:off+4096];protections[address]={'allocation':row['allocation'],'protect':row['protect']}
 return paths[prefix+'-cpu.bin'].read_bytes(),manifest,memory,protections
before,before_manifest,expected,old_protections=snapshots('library_patch_checkpoint','library-patch')
after,after_manifest,actual,protections=snapshots('library_patch_final','library-final')
trace_dir=out/'x86';subprocess.run([sys.executable,str(R/'scripts/trace-library-qol-patches.py'),str(source),'--output',str(trace_dir)],check=True)
trace_path=trace_dir/'result.json';trace=json.loads(trace_path.read_text());assert trace['completed_patch_routine'] and not trace['unconfirmed_writes']
for row in trace['changed_regions']:
 path=R/row['after_path'];assert sha(path)==row['after_sha256'];data=path.read_bytes();assert len(data)==row['size']
 for off in range(0,len(data),4096):expected[row['address']+off]=data[off:off+4096]
errors=[];memory_differences=[]
if set(actual)!=set(expected):errors.append({'kind':'mapped_pages','native_only':sorted(set(actual)-set(expected)),'x86_only':sorted(set(expected)-set(actual))})
for address in sorted(set(actual)&set(expected)):
 if actual[address]!=expected[address]:
  differences=[dict(address=address+i,native=x,x86=y) for i,(x,y) in enumerate(zip(actual[address],expected[address])) if x!=y]
  memory_differences.extend(differences)
reference_protections={int(k):v for k,v in trace['final_pages'].items()}
if protections!=reference_protections:errors.append({'kind':'page_permissions','differences':[dict(address=k,native=protections.get(k),x86=reference_protections.get(k)) for k in sorted(set(protections)|set(reference_protections)) if protections.get(k)!=reference_protections.get(k)]})
registers=['eax','ecx','edx','ebx','esp','ebp','esi','edi','eflags'];assert len(after)==len(before)==440
for index,name in enumerate(registers):
 value=struct.unpack_from('<I',after,index*4)[0]
 if value!=trace['final_registers'][name]:errors.append(dict(kind='register',name=name,native=value,x86=trace['final_registers'][name]))
# The finite native dispatcher uses one private word; it must identify the
# actual return boundary. Every other noninteger CPU byte stays unchanged.
mailbox=struct.unpack_from('<I',after,172)[0]
if mailbox!=after_manifest['resume_pc']:errors.append(dict(kind='dispatch_mailbox',native=mailbox,expected=after_manifest['resume_pc']))
for offset in list(range(36,172))+list(range(308,440)):
 if after[offset]!=before[offset]:errors.append(dict(kind='preserved_cpu_byte',offset=offset,before=before[offset],after=after[offset]))
for i,expected_xmm in enumerate(trace['final_xmm']):
 if after[176+16*i:192+16*i].hex()!=expected_xmm:errors.append(dict(kind='xmm',index=i))
if struct.unpack_from('<I',after,304)[0]!=trace['final_mxcsr']:errors.append(dict(kind='mxcsr'))
observed=run['library_code_writes'];reference=trace['code_writes'];assert len(observed)==len(reference)==25
for index,(native,x86) in enumerate(zip(observed,reference),1):
 if native['index']!=index or any(native[k]!=x86[k] for k in ['pc','address','size']) or native['value']!=int.from_bytes(bytes.fromhex(x86['after']),'little'):errors.append(dict(kind='code_write',native=native,x86=x86))
report=dict(passed=not errors and not memory_differences,run_sha256=sha(source),run_path=str(source.relative_to(R)),trace_sha256=sha(trace_path),script_sha256=sha(pathlib.Path(__file__).resolve()),instructions=trace['instructions'],services=len(trace['services']),code_writes=len(observed),guest_bytes_compared=sum(map(len,actual.values())),cpu_bytes=len(after),errors=errors,memory_differences=memory_differences,scope='Actual ARM64 library patch routine compared with original x86 from the same native checkpoint. All integer registers/flags, XMM/MXCSR, preserved extended CPU bytes, dispatch return, readable guest bytes, permissions and ordered code writes checked. Bounded explicit services; no full Windows/gameplay claim.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='memory_differences'},indent=2));print('memory differences',len(memory_differences));assert report['passed']
