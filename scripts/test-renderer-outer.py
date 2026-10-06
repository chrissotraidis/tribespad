#!/usr/bin/env python3
"""Differential x86 checks: CALL tails and whole blocks with removed calls."""
import ctypes,hashlib,json,mmap,pathlib,random,struct,subprocess
import unicorn
from unicorn import x86_const as X
R=pathlib.Path(__file__).resolve().parents[1];out=R/'generated/integration-review/renderer-outer';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();m=json.loads((out/'preparation.json').read_text())
for key,root in [('inputs_sha256',R),('outputs_sha256',out)]:
 for n,h in m[key].items():assert sha(root/n)==h,n
subprocess.run(['clang','-O2','-Wno-override-module','-c',str(out/'engine.ll'),'-o',str(out/'engine-test.o')],check=True)
targets=sorted({r['target'] for r in m['records']});bridge=out/'bridge.ll';bridge.write_text('@observed = global i32 0\n'+''.join('define hidden fastcc void @loc_%X(ptr %%cpu) {store i32 %d, ptr @observed\n ret void\n}\n'%(n,n) for n in targets))
libpath=out/'test.dylib';subprocess.run(['clang','-O2','-Wno-override-module','-dynamiclib',str(out/'tails.ll'),str(bridge),'-o',str(libpath)],check=True)
lib=ctypes.CDLL(str(libpath));seen=ctypes.c_uint32.in_dll(lib,'observed');memory=mmap.mmap(-1,0xa00000);base=ctypes.addressof(ctypes.c_char.from_buffer(memory));rng=random.Random(0x100478ba);regs=[getattr(X,'UC_X86_REG_'+n) for n in ['EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI','EFLAGS']]
for index,row in enumerate(m['records']):
 print('Checking',index,row['name'],flush=True)
 fn=getattr(lib,'tested_'+str(index));fn.argtypes=[ctypes.c_void_p];u=unicorn.Uc(unicorn.UC_ARCH_X86,unicorn.UC_MODE_32)
 start=row['call'] if row['kind']=='call' else row['entry'];code=bytes.fromhex(row['after_bytes'] if row['kind']=='call' else row['after_block'])
 regions=[(0x200000,65536),(0x795000,4096),(0x77f000,4096),(0x9e4000,4096)] if row['kind']=='nop' else [(0x200000,65536)]
 pages={start&~4095,(start+len(code))&~4095,row['target']&~4095,(row['target']&~4095)+4096}
 for at in pages:u.mem_map(at,4096)
 for at,n in regions:u.mem_map(at,n)
 u.mem_write(start,code);hit=[]
 def stop(c,a,n,_):
  if a==row['target']:hit.append(a);c.emu_stop()
 u.hook_add(unicorn.UC_HOOK_CODE,stop)
 for sample in range(256):
  for at,n in regions:
   data=rng.getrandbits(n*8).to_bytes(n,'little');u.mem_write(at,data);ctypes.memmove(base+at,data,n)
  state=ctypes.create_string_buffer(rng.getrandbits(3520).to_bytes(440,'little'),440);values=[rng.getrandbits(32) for _ in range(8)]+[0x202|(sample&0xd5)];values[4]=0x208000+sample;values[5]=0x204000+sample;struct.pack_into('<9I',state,0,*values);struct.pack_into('<Q',state,112,base);expected=bytearray(state.raw)
  for r,v in zip(regs,values):u.reg_write(r,v)
  hit.clear();u.emu_start(start,0,count=64);assert hit==[row['target']],(index,hit);struct.pack_into('<9I',expected,0,*[u.reg_read(r) for r in regs]);seen.value=0;fn(state);assert seen.value==row['target'] and state.raw==expected,(index,sample)
  for at,n in regions:assert ctypes.string_at(base+at,n)==bytes(u.mem_read(at,n)),(index,sample,hex(at))
(out/'test-result.json').write_text(json.dumps(dict(passed=True,cases=2304,call_tail_cases=1792,nop_whole_block_cases=512,preparation_sha256=sha(out/'preparation.json'),script_sha256=sha(pathlib.Path(__file__).resolve()),scope='Seven CALL tails with exact prefix identity, both historical entries in the 46-byte NOP span vs independent x86. All 440 CPU bytes/all flags,64KiB stack and touched global pages, varied unaligned stack/frame addresses. Not gameplay proof.'),indent=2)+'\n');print('PASS: 2304 native/x86 comparisons')
