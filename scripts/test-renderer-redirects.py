#!/usr/bin/env python3
"""Compare all five changed CALL tails to independent original x86 execution."""
import ctypes,hashlib,json,mmap,pathlib,random,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-redirects';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((out/'preparation.json').read_text())
for key,root in [('inputs_sha256',R),('outputs_sha256',out)]:
 for n,h in m[key].items():assert sha(root/n)==h,n
# Compile full variants too: unchanged prefixes may require original x87 helpers.
subprocess.run(['clang','-O2','-Wno-override-module','-c',str(out/'engine.ll'),'-o',str(out/'engine-test.o')],check=True)
bridge=out/'bridge.ll';bridge.write_text('@observed = global i32 0\n'+''.join('define hidden fastcc void @loc_%X(ptr %%cpu) {store i32 %d, ptr @observed\n ret void\n}\n'%(n,n) for n in [0x10031e30,0x10052700]))
libpath=out/'test.dylib';subprocess.run(['clang','-O2','-Wno-override-module','-dynamiclib',str(out/'tails.ll'),str(bridge),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));seen=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0x220000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));rng=random.Random(0x10032cbf);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for index,row in enumerate(m['records']):
 fn=getattr(lib,'tested_'+str(index));fn.argtypes=[ctypes.c_void_p];u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
 for at in [row['call']&~4095,row['target']&~4095]:u.mem_map(at,4096)
 u.mem_map(0x200000,65536);u.mem_write(row['call'],bytes.fromhex(row['after_bytes']));hit=[]
 def stop(c,a,n,_):
  if a==row['target']:hit.append(a);c.emu_stop()
 u.hook_add(unicorn.UC_HOOK_CODE,stop)
 for sample in range(256):
  data=rng.getrandbits(65536*8).to_bytes(65536,'little');u.mem_write(0x200000,data);ctypes.memmove(base+0x200000,data,len(data));state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);values=[rng.getrandbits(32) for _ in range(8)]+[0x202|(sample&0xd5)];values[4]=0x208000+sample;struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base);expected=bytearray(state.raw)
  for r,v in zip(regs,values):u.reg_write(r,v)
  hit.clear();u.emu_start(row['call'],0,count=2);assert hit==[row['target']];struct.pack_into('<9I',expected,0,*[u.reg_read(r) for r in regs]);seen.value=0;fn(state);assert seen.value==row['target'] and state.raw==expected,(index,sample);assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536))
(out/'test-result.json').write_text(json.dumps(dict(passed=True,cases=1280,preparation_sha256=sha(out/'preparation.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Five exact changed CALL tails vs x86: 440 CPU bytes,64KiB stack,unaligned ESP and flags. Prefix identity checked as exact IR replacement; not whole-function or gameplay proof.'),indent=2)+'\n');print('PASS: 1280 native/x86 CALL-tail comparisons')
