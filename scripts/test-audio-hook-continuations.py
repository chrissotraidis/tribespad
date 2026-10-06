#!/usr/bin/env python3
"""Compare each new audio continuation with patched original x86 execution."""
import ctypes,hashlib,json,mmap,pathlib,re,struct,subprocess
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];P=R/'generated/integration-review/audio-hook-bank/continuations';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((P/'result.json').read_text());assert sha(P/'module.ll')==m['module_sha256']
bridge=['@observed = global i32 0','@tribes_dispatch_count = constant i32 0','@tribes_dispatch_vas = constant [1 x i32] zeroinitializer','@tribes_dispatch_fns = global [1 x ptr] zeroinitializer','define void @tribes_return_to_host(ptr %cpu) { unreachable }','define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p = getelementptr i8, ptr %cpu, i64 172\n %v = load i32, ptr %p\n store i32 %v, ptr @observed\n ret void\n}']
for n in m['external']:bridge.append(f'define hidden fastcc void @{n}(ptr %cpu) {{store i32 {int(n[4:],16)}, ptr @observed\nret void\n}}')
for n in m['added']:bridge+=['declare hidden fastcc void @'+n+'(ptr)',f'define void @enter_{n}(ptr %cpu) {{call fastcc void @{n}(ptr %cpu)\nret void\n}}']
(P/'test-bridge.ll').write_text('\n'.join(bridge));sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
subprocess.run(['clang','-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),'-isysroot',sdk,'-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(P/'module.o'),str(P/'test-bridge.ll'),str(R/'port/core/guest_dispatch.c'),'-DPTROFS_64BIT','-o',str(P/'test.dylib')],check=True)
lib=ctypes.CDLL(str(P/'test.dylib'));seen=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0x100000000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));pe=pefile.PE(str(P/'Tribes2.exe'));image=pe.get_memory_mapped_image();text_end=0x400000+pe.sections[0].VirtualAddress+pe.sections[0].Misc_VirtualSize
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];targets={int(n[4:],16) for n in m['external']};bodies=(P/'module.ll').read_text();cases=0
for name in m['added']:
 fn=getattr(lib,'enter_'+name);fn.argtypes=[ctypes.c_void_p];entry=int(name[4:],16)
 for sample in range(64):
  u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);pages={};hit=[]
  def page(at):
   at &=~4095
   if at in pages:return
   data=image[at-0x400000:at-0x400000+4096] if 0x400000<=at<text_end else struct.pack('<I',0x220100+sample*16)*1024
   assert len(data)==4096;u.mem_map(at,4096);u.mem_write(at,data);pages[at]=data
  def unmapped(c,access,a,n,value,_):
   for at in range(a&~4095,(a+n+4095)&~4095,4096):page(at)
   return True
  u.hook_add(unicorn.UC_HOOK_MEM_UNMAPPED,unmapped);page(entry)
  values=[0x220100+sample*16]*8+[0x202|(sample&0xd5)];values[4]=0x208800+sample;values[5]=0x208000+sample;values[6]=sample
  for reg,v in zip(regs,values):u.reg_write(reg,v)
  def code(c,a,n,_):
   if a in targets or not entry<=a<entry+512:hit.append(a);c.emu_stop()
  u.hook_add(unicorn.UC_HOOK_CODE,code);u.emu_start(entry,0,count=256);assert len(hit)==1,(name,sample)
  for at,data in pages.items():ctypes.memmove(base+at,data,4096)
  cpu=ctypes.create_string_buffer(440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);expected=bytearray(cpu.raw);struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs])
  begin=re.search(r'define (?:hidden|protected) fastcc void @'+name+r'\(',bodies).start();body=bodies[begin:bodies.index('\n}',begin)]
  if '@tribes_set_dispatch_target(' in body:struct.pack_into('<I',expected,172,hit[0])
  seen.value=0;fn(cpu);assert cpu.raw==expected and seen.value==hit[0],(name,sample,hex(seen.value),hex(hit[0]),[(i,a,b) for i,(a,b) in enumerate(zip(cpu.raw,expected)) if a!=b])
  for at in pages:assert ctypes.string_at(base+at,4096)==bytes(u.mem_read(at,4096)),(name,sample,hex(at))
  cases+=1
report=dict(passed=True,cases=cases,entries=len(m['added']),module_sha256=sha(P/'module.ll'),preparation_sha256=sha(P/'result.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Seven whole original continuations vs patched x86,64 varied integer/flag/unaligned-stack states each. All440 CPU bytes,control transfers and touched memory checked; callees remain boundaries,not gameplay acceptance.')
(P/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',cases,'native/x86 continuation cases')
