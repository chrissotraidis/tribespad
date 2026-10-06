#!/usr/bin/env python3
"""Execute every added original continuation, including the exact EB16 offcut."""
import ctypes,hashlib,json,mmap,pathlib,random,struct,subprocess
import pefile,unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-hook-bank/continuations';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((out/'result.json').read_text());assert sha(out/'module.ll')==m['module_sha256']
names=m['added'];targets={int(n[4:],16) for n in m['external']};indirect=0x900000
bridge=['@observed = global i32 0','@tribes_dispatch_count = constant i32 0','@tribes_dispatch_vas = constant [1 x i32] zeroinitializer','@tribes_dispatch_fns = global [1 x ptr] zeroinitializer','define void @tribes_return_to_host(ptr %cpu) { unreachable }','define hidden fastcc void @tribes_dispatch(ptr %cpu) { %p = getelementptr i8, ptr %cpu, i64 172\n %v = load i32, ptr %p\n store i32 %v, ptr @observed\n ret void\n}']
for n in m['external']:bridge.append('define hidden fastcc void @%s(ptr %%cpu) {store i32 %d, ptr @observed\nret void\n}'%(n,int(n[4:],16)))
for i,n in enumerate(names):bridge+=['declare hidden fastcc void @%s(ptr)'%n,'define void @enter%d(ptr %%cpu) {call fastcc void @%s(ptr %%cpu)\nret void\n}'%(i,n)]
(out/'test-bridge.ll').write_text('\n'.join(bridge));subprocess.run(['clang','-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),'-O2','-Wno-override-module','-dynamiclib',str(out/'module.o'),str(out/'test-bridge.ll'),str(R/'port/core/guest_dispatch.c'),'-DPTROFS_64BIT','-o',str(out/'test.dylib')],check=True)
lib=ctypes.CDLL(str(out/'test.dylib'));seen=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0xb00000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));pe=pefile.PE(str(out/'Tribes2.exe'));image=pe.get_memory_mapped_image();u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x400000,(len(image)+4095)&~4095);u.mem_write(0x400000,image)
regions=[(0x200000,65536),(0x220000,65536),(0x757000,4096),(0x785000,4096),(0x7ed000,4096),(0x9a7000,4096),(0x9e8000,4096),(0x9e9000,4096)]
for at,n in regions[:2]:u.mem_map(at,n)
# The observer target lies inside the mapped PE; stop before its bytes execute.
hit=[]
def stop(c,a,n,_):
 if a in targets or a==indirect:hit.append(a);c.emu_stop()
u.hook_add(unicorn.UC_HOOK_CODE,stop);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(0x6b0c21);cases=0
for i,name in enumerate(names):
 fn=getattr(lib,'enter%d'%i);fn.argtypes=[ctypes.c_void_p]
 for sample in range(128):
  for at,n in regions:
   data=struct.pack('<I',0x220800)*(n//4) if at<0x400000 else rng.getrandbits(n*8).to_bytes(n,"little");u.mem_write(at,data);ctypes.memmove(base+at,data,n)
  def write(at,data):u.mem_write(at,data);ctypes.memmove(base+at,data,len(data))
  values=[sample,0x220100,0x220400,0x220100,0x208000+4*(sample%16),0x204000+4*(sample%16),sample,0x220800,0x202|(sample&0x8d5)]
  write(0x22016c,struct.pack('<I',0x220000))
  for at in [0x9e8854,0x9e91ac,0x7855c4]:write(at,struct.pack('<I',indirect))
  for at in [0x220808,0x220174,0x220864,0x220300]:write(at,struct.pack('<I',[0,3,0xffffffff,0x80000000][sample%4]))
  write(0x2201ce,bytes([sample%2]));write(0x757908,bytes([sample%2]));write(0x9a73bc,struct.pack('<I',0x10000000 if sample%2 else 0))
  cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,"little"),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);expected=bytearray(cpu.raw)
  for reg,v in zip(regs,values):u.reg_write(reg,v)
  hit.clear();u.emu_start(int(name[4:],16),0,count=128);assert len(hit)==1,(name,sample,hit)
  struct.pack_into('<9I',expected,0,*[u.reg_read(r) for r in regs])
  if hit[0]==indirect:struct.pack_into('<I',expected,172,indirect)
  seen.value=0;fn(cpu);assert seen.value==hit[0] and cpu.raw==expected,(name,sample,hex(hit[0]),hex(seen.value),[(k,a,b) for k,(a,b) in enumerate(zip(cpu.raw,expected)) if a!=b])
  for at,n in regions:assert ctypes.string_at(base+at,n)==bytes(u.mem_read(at,n)),(name,sample,hex(at))
  cases+=1
report=dict(passed=True,cases=cases,entries=len(names),module_sha256=sha(out/'module.ll'),preparation_sha256=sha(out/'result.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Every added continuation executed against original x86 up to existing function boundaries; exact EB16 and offcut21 executed, all CPU bytes/flags, stack and touched data agree. Not complete callee or game acceptance.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'original continuation comparisons')
