#!/usr/bin/env python3
"""Resume original x86 patch code from a checked native pre-write checkpoint.

Only declared services run. Unknown calls/instructions stop; no native guest
code protections are relaxed and no captured routes become product inputs.
"""
import argparse,hashlib,json,pathlib,struct
import capstone,pefile,unicorn
from unicorn import x86_const as X
from guest_page_contract import GuestPages
from guest_cache_reference import ReferenceInstructionCache
from guest_teb_contract import configure_sparse_teb
R=pathlib.Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
ap=argparse.ArgumentParser();ap.add_argument('checkpoint',type=pathlib.Path);ap.add_argument('--budget',type=int,default=1000000);ap.add_argument('--output',type=pathlib.Path);a=ap.parse_args();source=a.checkpoint.resolve();run=json.loads(source.read_text());assert run['initialized'] and run['result']['kind'] in ['network_patch_checkpoint','network_patch_native_return']
inputs=run['network_patch_checkpoint'];paths={}
for name,row in inputs.items():
 p=R/row['path'];assert p.stat().st_size==row['bytes'] and sha(p)==row['sha256'];paths[name]=p
manifest=json.loads(paths['network-patch-pages.json'].read_text());cpu=paths['network-patch-cpu.bin'].read_bytes();assert len(cpu)==manifest['cpu_bytes']==440
out=a.output.resolve() if a.output else R/'generated/integration-review/network-patch-trace';out.mkdir(exist_ok=True)
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);pages=GuestPages(u);initial={}
for row in manifest['ranges']:
 address,size=row['address'],row['size'];data=paths[row['file']].read_bytes();assert len(data)==size and not address%4096 and not size%4096
 u.mem_map(address,size);u.mem_write(address,data);pages.declare(address,size,row['protect'],row['allocation']);initial[address]=data
code_ranges=[];image_ranges=[]
for name,path in [('engine',R/'ref/control/Tribes2.exe'),('qol',R/'ref/product/IFC22.dll')]:
 pe=pefile.PE(str(path));base=pe.OPTIONAL_HEADER.ImageBase;image_ranges.append((name,base,base+pe.OPTIONAL_HEADER.SizeOfImage))
 for s in pe.sections:
  if s.Characteristics&0x20000000:
   start=base+s.VirtualAddress;size=max(s.Misc_VirtualSize,s.SizeOfRawData);code_ranges.append((name,start,start+size))
   # Native PEs are data. Add execution only in this x86 reference process,
   # preserving captured logical protection values for VirtualProtect outputs.
   for p in range(start&~4095,(start+size+4095)&~4095,4096):
    if p in pages.pages:
     from guest_page_contract import PERMISSIONS
     u.mem_protect(p,4096,PERMISSIONS[pages.pages[p][1]]|unicorn.UC_PROT_EXEC)
bank=run['patch_inputs']['native_allocation']['address'] if 'native_allocation' in run['patch_inputs'] else 0x3c0000
# The trace is bounded to this patch routine and its actual DLL callees.
stop_pc=0x1006102c
imports={}
for p in [R/'generated/srw/run-20261002T220330-ifc22/native-diagnostic/result.json',R/'generated/integration-review/patched-engine/va/result.json']:
 for row in json.loads(p.read_text())['imports']:imports[row['guest_va']]=row
u.mem_map(0xfff00000,0x100000,unicorn.UC_PROT_ALL)
gdt=0x3b0000;assert gdt not in pages.pages;u.mem_map(gdt,4096)
teb=bytes(u.mem_read(0x3d0000,4096));fields={off:struct.unpack_from('<I',teb,off)[0] for off in (4,8,0x20,0x24,0x2c)}
configure_sparse_teb(u,0x3d0000,gdt,fields[4],fields[8],fields[0x20],fields[0x24],fields[0x2c]);u.mem_write(0x3d0000,teb)
registers=['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']
for index,name in enumerate(registers):u.reg_write(getattr(X,'UC_X86_REG_'+name),struct.unpack_from('<I',cpu,4*index)[0])
assert struct.unpack('<I',u.mem_read(u.reg_read(X.UC_X86_REG_EBP)+4,4))[0]==0x100292b8
outer_ebp=struct.unpack('<I',u.mem_read(u.reg_read(X.UC_X86_REG_EBP),4))[0]
assert struct.unpack('<I',u.mem_read(outer_ebp+4,4))[0]==stop_pc
for index in range(8):u.reg_write(getattr(X,'UC_X86_REG_XMM%d'%index),int.from_bytes(cpu[176+16*index:192+16*index],'little'))
u.reg_write(X.UC_X86_REG_MXCSR,struct.unpack_from('<I',cpu,304)[0])
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);cache=ReferenceInstructionCache(u,pages)
writes=[];pending=[];services=[];recent=[];stop=None;instructions=0

def finish():
 for e in pending:
  e['after']=bytes(u.mem_read(e['address'],e['size'])).hex();e['completed']=True;writes.append(e)
 pending.clear()

def service(token):
 global stop
 row=imports[token];name=row['name'];sp=u.reg_read(X.UC_X86_REG_ESP);target=struct.unpack('<I',u.mem_read(sp,4))[0];argc=0;result=None;event={'name':name,'pc':token,'return_pc':target}
 if name=='VirtualProtect':
  argc=4;args=struct.unpack('<4I',u.mem_read(sp+4,16));event['arguments']=args
  event['effect']=pages.protect(*args);result=1
 elif name=='GetCurrentProcess':result=0xffffffff
 elif name=='GetCurrentThread':result=0xfffffffe
 elif name=='GetCurrentThreadId':result=fields[0x24]
 elif name=='FlushInstructionCache':
  process,address,size=struct.unpack('<3I',u.mem_read(sp+4,12));assert process==0xffffffff
  stop={'kind':'flush_pending','sp':sp,'return_pc':target,'address':address,'size':size};u.emu_stop();return
 else:
  stop={'kind':'unimplemented_service','name':name,'pc':token,'return_pc':target,'arguments':list(struct.unpack('<8I',u.mem_read(sp+4,32)))};u.emu_stop();return
 event['result']=result;services.append(event);u.reg_write(X.UC_X86_REG_EAX,result);u.reg_write(X.UC_X86_REG_ESP,sp+4*(argc+1));u.reg_write(X.UC_X86_REG_EIP,target)

def code(uc,address,size,unused):
 global stop,instructions
 finish();instructions+=1
 if address==stop_pc:stop={'kind':'patch_routine_return','pc':address};uc.emu_stop();return
 if address in imports:
  try:service(address)
  except (ValueError,AssertionError,unicorn.UcError) as error:stop={'kind':'service_failure','pc':address,'error':str(error)};uc.emu_stop()
  return
 if not any(owner=='qol' and start<=address<end for owner,start,end in code_ranges):stop={'kind':'outside_patch_dll','pc':address};uc.emu_stop();return
 ins=next(md.disasm(bytes(uc.mem_read(address,size)),address),None);assert ins and ins.size==size
 recent.append({'pc':address,'instruction':ins.mnemonic+' '+ins.op_str});del recent[:-32]
 # GPR/XMM/MXCSR are restored. Only known vector moves are admitted here.
 if any(r in ins.op_str.split(', ') for r in ('mm0','mm1','mm2','mm3','mm4','mm5','mm6','mm7')) or ins.mnemonic.startswith('f') or (ins.mnemonic not in ('movq','movups','movaps','movdqu','movdqa','movlpd','xorps') and any(r in ins.op_str for r in ('xmm','ymm','zmm','mm0','mm1','mm2','mm3','mm4','mm5','mm6','mm7'))):
  stop={'kind':'unrestored_fp_vector_state','pc':address,'instruction':recent[-1]};uc.emu_stop()

def write(uc,access,address,size,value,unused):
 owner=next((n for n,lo,hi in code_ranges if lo<=address<hi),'bank' if bank<=address<bank+65536 else None)
 category='code'
 if owner is None:
  owner=next((n for n,lo,hi in image_ranges if lo<=address<hi),None);category='image_data'
 if owner:pending.append({'category':category,'image':owner,'pc':uc.reg_read(X.UC_X86_REG_EIP),'address':address,'size':size,'before':bytes(uc.mem_read(address,size)).hex(),'value':value})

def invalid(uc,access,address,size,value,unused):
 global stop
 stop={'kind':'invalid_memory','pc':uc.reg_read(X.UC_X86_REG_EIP),'access':access,'address':address,'size':size};return False
u.hook_add(unicorn.UC_HOOK_CODE,code);u.hook_add(unicorn.UC_HOOK_MEM_WRITE,write);u.hook_add(unicorn.UC_HOOK_MEM_INVALID,invalid)
resume=manifest['resume_pc']
while instructions<a.budget:
 cache.running=True
 try:u.emu_start(resume,0,count=a.budget-instructions)
 except unicorn.UcError as error:
  if stop is None:stop={'kind':'cpu_error','pc':u.reg_read(X.UC_X86_REG_EIP),'error':str(error)}
 finally:cache.running=False
 if not stop or stop['kind']!='flush_pending':break
 effect=cache.invalidate(stop['address'],stop['size']);services.append({'name':'FlushInstructionCache','effect':effect,'return_pc':stop['return_pc'],'result':1})
 u.reg_write(X.UC_X86_REG_EAX,1);u.reg_write(X.UC_X86_REG_ESP,stop['sp']+16);u.reg_write(X.UC_X86_REG_EIP,stop['return_pc']);resume=stop['return_pc'];stop=None
if stop is None:stop={'kind':'budget','pc':u.reg_read(X.UC_X86_REG_EIP)}
changed=[]
for row in manifest['ranges']:
 address,size=row['address'],row['size'];after=bytes(u.mem_read(address,size));before=initial[address]
 if after!=before:
  path=out/row['file'];path.write_bytes(after);changed.append(dict(row,after_path=str(path.relative_to(R)),after_sha256=sha(path),changed_bytes=sum(a!=b for a,b in zip(before,after))))
report={'state':'finished','completed_patch_routine':stop['kind']=='patch_routine_return','stop':stop,'instructions':instructions,'checkpoint_sha256':sha(source),'checkpoint_path':str(source.relative_to(R)),'checkpoint_inputs':inputs,'script_sha256':sha(pathlib.Path(__file__).resolve()),'unicorn_version':unicorn.__version__,'services':services,'code_writes':[e for e in writes if e['category']=='code'],'image_data_writes':[e for e in writes if e['category']=='image_data'],'final_pages':{str(address):{'allocation':data[0],'protect':data[1]} for address,data in pages.pages.items()},'final_registers':{name.lower():u.reg_read(getattr(X,'UC_X86_REG_'+name)) for name in registers},'final_xmm':[u.reg_read(getattr(X,'UC_X86_REG_XMM%d'%i)).to_bytes(16,'little').hex() for i in range(8)],'final_mxcsr':u.reg_read(X.UC_X86_REG_MXCSR),'unconfirmed_writes':pending,'changed_regions':changed,'recent':recent,'scope':'Original x86 resumed from actual native checkpoint; integer/XMM/MXCSR state, explicit page protection and cache services, unknown APIs/FP/vector operations stop. No full Windows reference or native late-patch activation.'}
report['helper_sources_sha256']={name:sha(R/'scripts'/name) for name in ['guest_page_contract.py','guest_cache_reference.py','guest_teb_contract.py']}
report['original_images_sha256']={name:sha(R/path) for name,path in [('engine','ref/control/Tribes2.exe'),('qol','ref/product/IFC22.dll')]}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ['completed_patch_routine','stop','instructions']},indent=2));print('writes',len(writes),'services',len(services))
