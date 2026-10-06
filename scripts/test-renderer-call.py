#!/usr/bin/env python3
"""Compare the exact changed CALL tail with independent x86 execution."""
import ctypes,hashlib,json,mmap,pathlib,random,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-call';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((out/'preparation.json').read_text())
for key,root in [('inputs_sha256',R),('outputs_sha256',out)]:
 for n,h in m[key].items():assert sha(root/n)==h,n
bridge=out/'bridge.ll';bridge.write_text('@observed = global i32 0\ndefine hidden fastcc void @loc_10046600(ptr %cpu) {store i32 268723712, ptr @observed\n ret void\n}\n')
libpath=out/'test.dylib';subprocess.run(['clang','-target','arm64-apple-macos12','-O2','-Wno-override-module','-dynamiclib',str(out/'call-tail.ll'),str(bridge),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));fn=lib.tested_call;fn.argtypes=[ctypes.c_void_p];seen=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0x220000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));rng=random.Random(0x10047071)
u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32);u.mem_map(0x505000,4096);u.mem_map(0x10046000,4096);u.mem_map(0x200000,65536);u.mem_write(0x505d4b,bytes.fromhex('e8b008b40f'));u.mem_write(0x10046600,b'\xcc');hit=[]
def stop(c,a,n,_):
 if a==0x10046600:hit.append(a);c.emu_stop()
u.hook_add(unicorn.UC_HOOK_CODE,stop);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for sample in range(256):
 data=rng.getrandbits(65536*8).to_bytes(65536,'little');u.mem_write(0x200000,data);ctypes.memmove(base+0x200000,data,len(data));state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);values=[rng.getrandbits(32) for _ in range(8)]+[0x202|(sample&0xd5)];values[4]=0x208000+sample;struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base);expected=bytearray(state.raw)
 for r,v in zip(regs,values):u.reg_write(r,v)
 hit.clear();u.emu_start(0x505d4b,0,count=2);assert hit==[0x10046600];struct.pack_into('<9I',expected,0,*[u.reg_read(r) for r in regs]);seen.value=0;fn(state);assert seen.value==0x10046600 and state.raw==expected;assert ctypes.string_at(base+0x200000,65536)==bytes(u.mem_read(0x200000,65536))
(out/'test-result.json').write_text(json.dumps(dict(passed=True,cases=256,preparation_sha256=sha(out/'preparation.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),tail_sha256=sha(out/'call-tail.ll'),scope='Exact transformed CALL tail versus independent x86; 440 CPU bytes,64KiB stack,unaligned ESP and flags. Prefix unchanged by exact IR comparison; no whole-function or game acceptance claim.'),indent=2)+'\n');print('PASS: 256 renderer CALL tail native/x86 comparisons')
