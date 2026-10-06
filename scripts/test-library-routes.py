#!/usr/bin/env python3
"""Execute every complete routed native block against original/patched x86."""
import ctypes,hashlib,json,pathlib,random,re,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/library-routes';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();meta=json.loads((out/'result.json').read_text());assert sha(out/'module.ll')==meta['module_sha256'];capture=json.loads((R/meta['checkpoint_path']).read_text());rows=meta['routes'];targets=sorted({r[k] for r in rows for k in ['old_target','new_target']})
bridge=['@observed = global i32 0']
for t in targets:bridge.append('define hidden fastcc void @loc_%X(ptr %%cpu) { store i32 %d, ptr @observed\n ret void\n}'%(t,t))
for i,r in enumerate(rows):bridge+=['declare hidden fastcc void @%s(ptr)'%r['name'],'define void @enter_%d(ptr %%cpu) { call fastcc void @%s(ptr %%cpu)\n ret void\n}'%(i,r['name'])]
(out/'bridge.ll').write_text('\n'.join(bridge)+'\n');helper=re.search(r'^int qol_library_route_ready\(void\)\{[^\n]+',(R/'tests/qol_native_bootstrap.c').read_text(),re.M)[0];(out/'phase.c').write_text('#include <stdint.h>\n#include <assert.h>\nstatic uint64_t base=1; unsigned phase;\nuint32_t tribes_memory_code_revision(uint64_t b){assert(b==base);return phase;}\n'+helper+'\n')
libpath=out/'test.dylib';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'module.ll'),str(out/'bridge.ll'),str(out/'phase.c'),'-o',str(libpath)],check=True);lib=ctypes.CDLL(str(libpath));phase=ctypes.c_uint32.in_dll(lib,'phase');observed=ctypes.c_uint32.in_dll(lib,'observed')
memory=ctypes.create_string_buffer(0x1000000);base=ctypes.addressof(memory);u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x200000,65536)
manifest=json.loads((R/capture['library_patch_checkpoint']['library-patch-pages.json']['path']).read_text())
for row in manifest['ranges']:
 if not 0x400000<=row['address']<0x1000000:continue
 item=capture['library_patch_checkpoint'][row['file']];p=R/item['path'];assert sha(p)==item['sha256'];data=p.read_bytes();u.mem_map(row['address'],row['size']);u.mem_write(row['address'],data);ctypes.memmove(base+row['address'],data,len(data))
u.mem_map(0x10000000,0x200000);stop=[0]
def code(machine,pc,size,_):
 if pc in targets:stop[0]=pc;machine.emu_stop()
u.hook_add(unicorn.UC_HOOK_CODE,code);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']];rng=random.Random(25034);cases=0
for state in [1,2,4,6]:
 phase.value=state
 if state==6:
  for r in rows:
   w=r['writer'];u.mem_write(w['address'],bytes.fromhex(w['after']));u.ctl_remove_cache(r['entry'],r['site']+5)
 for i,r in enumerate(rows):
  fn=getattr(lib,'enter_%d'%i);fn.argtypes=[ctypes.c_void_p]
  for sample in range(32):
   # Pointer-bearing fields form a bounded, varied object graph. Every complete
   # block executes original argument/stack/object writes before its call.
   pattern=b''.join(struct.pack('<I',0x20b000+4*rng.randrange(3072)) for _ in range(16384));u.mem_write(0x200000,pattern);ctypes.memmove(base+0x200000,pattern,65536)
   values=[0x202000+0x800*j+(sample%16)*4 for j in range(8)]+[0x202|rng.choice([0,0x8d5,0x41,0x884,0x400])];values[4]=0x208000+(sample%16)*4;values[5]=0x208800
   cpu=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);struct.pack_into('<9I',cpu,0,*values);struct.pack_into('<Q',cpu,112,base);saved=cpu.raw
   for reg,value in zip(regs,values):u.reg_write(reg,value)
   stop[0]=0;u.emu_start(r['entry'],0,count=128);expected=r['new_target'] if state==6 else r['old_target'];assert stop[0]==expected,(r['name'],state,'x86 stop',hex(stop[0]));observed.value=0;fn(cpu)
   assert observed.value==expected,(r['name'],state,'native target');assert list(struct.unpack_from('<9I',cpu))==[u.reg_read(reg) for reg in regs],(r['name'],state,'registers');assert cpu.raw[36:]==saved[36:],(r['name'],state,'extended CPU');assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536)),(r['name'],state,'stack/objects');cases+=1
report=dict(passed=True,cases=cases,states=[1,2,4,6],definitions=len(rows),variant_sha256=sha(out/'result.json'),module_sha256=sha(out/'module.ll'),binary_sha256=sha(libpath),harness_sha256=sha(R/'tests/qol_native_bootstrap.c'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Every exact complete modified LLVM block and finite call router executes against original and patched x86. Target, full GPR/flags, preserved CPU and64KiB object/stack bytes agree. Replacement callees are observers, not gameplay execution.')
(out/'test-result.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS:',cases,'native/x86 complete-block comparisons across25 redirects and4 publication states')
