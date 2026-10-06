#!/usr/bin/env python3
"""Compare actual translated Miles directory/callback/timer leaves with their x86 bytes."""
import ctypes,hashlib,json,mmap,pathlib,random,re,struct,subprocess
import unicorn
from unicorn import x86_const as X
from miles_runtime_inputs import R,SOURCE,sha
from sse_build_inputs import sse_inputs
m=json.loads((SOURCE/'result.json').read_text());assert sha(SOURCE/'module.ll')==m['module_sha256']
out=R/'generated/integration-review/miles/startup-leaves';out.mkdir(parents=True,exist_ok=True)
ir=(SOURCE/'module.ll').read_text();blocks={x[1]:x[0] for x in re.finditer(r'^define hidden fastcc void @(loc_[0-9A-F]+)\(.*?^}',ir,re.M|re.S)}
exports=['_AIL_set_redist_directory@4','_AIL_mem_use_malloc@4','_AIL_mem_use_free@4','_AIL_get_timer_highest_delay@0'];addresses=[m['exports'][n] for n in exports]
selected=set();pending=['loc_%X'%v for v in addresses]
while pending:
 n=pending.pop()
 if n in selected:continue
 selected.add(n);pending.extend(set(re.findall(r'@(loc_[0-9A-F]+)\(',blocks[n]))-selected)
module='\n'.join(line for line in ir.splitlines() if line.startswith('declare ') or re.match(r'^%\w+ = type ',line))+'\n'+'\n'.join(blocks[n] for n in sorted(selected))
(out/'leaves.ll').write_text(module)
bridge=['@observed = global i32 0','@tribes_dispatch_count = constant i32 0','@tribes_dispatch_vas = constant [1 x i32] zeroinitializer','@tribes_dispatch_fns = global [1 x ptr] zeroinitializer','define void @tribes_return_to_host(ptr %cpu) { unreachable }','define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p = getelementptr i8, ptr %cpu, i64 172\n %v = load i32, ptr %p\n store i32 %v, ptr @observed\n ret void\n}']
for i,v in enumerate(addresses):bridge+=['declare hidden fastcc void @loc_%X(ptr)'%v,'define void @enter%d(ptr %%cpu){call fastcc void @loc_%X(ptr %%cpu)\nret void\n}'%(i,v)]
(out/'bridge.ll').write_text('\n'.join(bridge));libpath=out/'test.dylib'
math=sse_inputs('macos')
subprocess.run(['clang','-O2','-Wno-override-module','-dynamiclib',str(out/'leaves.ll'),str(out/'bridge.ll'),str(R/'port/core/guest_dispatch.c'),str(R/'port/llasm-support/tribes_sse.c'),*math['compile_flags'],*math['sources'],*math['libraries'],'-DPTROFS_64BIT','-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));observed=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0x21200000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory))
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);image=(SOURCE/'image.bin').read_bytes();u.mem_map(m['image_base'],m['image_size']);u.mem_write(m['image_base'],image)
for addr in [0x200000,0x220000,0x900000]:u.mem_map(addr,65536)
u.hook_add(unicorn.UC_HOOK_CODE,lambda uc,a,n,data:uc.emu_stop() if a==0x900000 else None)
regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(0x21128e00);cases=0
regions=[(0x200000,65536),(0x220000,65536),(0x21166000,0x7000)]
def write(addr,data):u.mem_write(addr,data);ctypes.memmove(base+addr,data,len(data))
for index,entry in enumerate(addresses):
 fn=getattr(lib,'enter%d'%index);fn.argtypes=[ctypes.c_void_p]
 for sample in range(128):
  for addr,size in regions:write(addr,rng.getrandbits(size*8).to_bytes(size,'little'))
  values=[rng.getrandbits(32) for _ in regs];values[4]=0x208004;values[8]=0x202|(sample&0x8d5)
  argument=0 if sample%4==0 else 0x220101 if index==0 else rng.getrandbits(32)
  if index==0:
   value=[b'',b'.',b'C:',b'C:\\Audio\\',b'folder/',b'\\\\server\\folder',b'x'*245][sample%7]
   write(0x220101,value+b'\0')
  write(values[4],struct.pack('<II',0x900000,argument))
  if index==3:
   write(0x2116a9b8,struct.pack('<I',[0,100,0xffffffff,sample][sample%4]));write(0x211691f8,struct.pack('<I',[0,99,0xffffffff,100][sample%4]))
  cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);expected=bytearray(cpu.raw)
  for reg,value in zip(regs,values):u.reg_write(reg,value)
  u.emu_start(entry,0,count=4096);assert u.reg_read(X.UC_X86_REG_EIP)==0x900000
  struct.pack_into('<9I',expected,0,*[u.reg_read(reg) for reg in regs]);struct.pack_into('<I',expected,172,0x900000)
  observed.value=0;fn(cpu);assert observed.value==0x900000 and cpu.raw==expected,(exports[index],sample)
  for addr,size in regions:assert ctypes.string_at(base+addr,size)==bytes(u.mem_read(addr,size)),(exports[index],sample,hex(addr))
  cases+=1
report=dict(passed=True,cases=cases,exports=exports,module_sha256=m['module_sha256'],test_binary_sha256=sha(libpath),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Actual native AOT versus original x86 leaves: entire CPU, flags, stdcall stack and data regions. Includes null/reset and callback replacement. Not DLL initialization or audio playback.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'original Miles leaf comparisons')
